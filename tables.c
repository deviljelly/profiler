#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "tables.h"
#include "util.h"

MethodIDHashtable *methodIDHashtable;
ClassHashtable *classHashtable;
ThreadHashtable *threadHashtable;

void createMethodIDHashtable() {
    methodIDHashtable = calloc(1, sizeof(MethodIDHashtable));
    if(methodIDHashtable<=0) {
        exit(-1);
    }

}

void createClassHashtable() {
    classHashtable = calloc(1, sizeof(ClassHashtable));
    if(classHashtable<=0) {
        exit(-1);
    }

}

void createThreadHashtable() {
    threadHashtable = calloc(1, sizeof(ThreadHashtable));
    if(threadHashtable<=0) {
        exit(-1);
    }

}

ClassNode* getClassNode(char *name) {

    uint32_t hashCode = jenkins_one_at_a_time_hash(name, strlen(name));
    uint32_t key = hashCode & CLASS_HASHTABLE_MASK;

    ClassBucket *bucket = classHashtable->buckets[key];

    if(bucket>0) {

        lock(&bucket->lock, false);

        uint64_t start = getTicks();

        ClassNode *node = bucket->rootNode;
        while(node>0) {

            if(node->hashCode==hashCode) {
                uint32_t i = strlen(name);
                uint32_t j = strlen((const char*)node->name);
                if(i==j) {

                    uint32_t match = 1;
                    i=0;

                    while((i<j) && match==1) {

                        if(node->name[i] != name[i]) {
                            match = 0;
                        }
                        i++;
                    }

                    if(match) {
                        classHashtable->lockedFor += getTicks() - start;
                        unlock(&bucket->lock, false);
                        return node;
                    }

                }
            }

            node = node->next;
        }

        classHashtable->lockedFor += getTicks() - start;
        unlock(&bucket->lock, false);
    }

    return NULL;

}

MethodIDNode* getMethodIDNode(jmethodID jvmtiMethodID) {

    if(jvmtiMethodID<=0) return NULL;

    uint64_t hashCode = hashUint64((uintptr_t)jvmtiMethodID);
    uint32_t key = (uint32_t) (hashCode & METHOD_ID_HASHTABLE_MASK);

    volatile MethodIDBucket *bucket = methodIDHashtable->buckets[key];

    if(bucket>0) {

        volatile MethodIDNode *node = bucket->rootNode;

        while(node>0) {

            if(node->jvmtiMethodID == jvmtiMethodID) {
                return (MethodIDNode*)node;
            }

            node = node->next;
        }

    }

    return NULL;

}

ThreadNode* getThreadNode(uint32_t threadID) {

    uint64_t hashCode = hashUint64((uint64_t) threadID);
    uint32_t key = hashCode & THREAD_HASHTABLE_MASK;

    ThreadBucket *bucket = threadHashtable->buckets[key];

    if(bucket>0) {

        lock(&bucket->lock, false);
        ThreadNode *node = bucket->rootNode;
        while(node>0) {

            if(node->threadID == threadID) {
                unlock(&bucket->lock, false);
                return node;
            }

            node = node->next;
        }

        unlock(&bucket->lock, false);
    }

    return NULL;

}

void addListToMethodIDHashtable(uint32_t numberOfMethods, MethodIDNode *methodIDNodeList, jvmtiEnv *jvmtiInterface) {

    lock(&methodIDHashtable->lock, false);

    for (int i = 0; i < numberOfMethods; i++) {

        uint64_t hashCode = hashUint64((uintptr_t) methodIDNodeList[i].jvmtiMethodID);
        uint32_t key = (uint32_t) (hashCode & METHOD_ID_HASHTABLE_MASK);

        MethodIDBucket *bucket = methodIDHashtable->buckets[key];

        if (methodIDHashtable->buckets[key] <= 0) {
           bucket = calloc(1, sizeof(MethodIDBucket));
            if (bucket <= 0) {
                error("Unable to allocate MethodIDBucket\n");
                unlock(&methodIDHashtable->lock, false);
                return;
            }

           if (!compareAndSwapPtrBool((volatile uintptr_t *) &methodIDHashtable->buckets[key], NULL, bucket)) {
                error("Bucket was supposed to be NULL!\n");
            }

        }

        volatile MethodIDNode *newNode = &methodIDNodeList[i];
        if (!compareAndSwapPtrBool((volatile uintptr_t*) &bucket->rootNode, NULL, newNode)) {

            volatile MethodIDNode *node = bucket->rootNode;
            while (!compareAndSwapPtrBool((volatile uintptr_t *) &node->next, NULL, newNode)) {

                node = node->next;
            }

        }

        bucket->chainLength++;

    }

    unlock(&methodIDHashtable->lock, false);

}



void addToMethodIDHashtable(jmethodID jvmtiMethodID, jclass jvmtiClass, uint16_t classID, uint16_t methodID) {

    uint64_t hashCode = hashUint64((uint64_t)jvmtiMethodID);
    uint32_t key = (uint32_t) (hashCode & METHOD_ID_HASHTABLE_MASK);

    lock(&methodIDHashtable->lock, false);

    MethodIDBucket *bucket = methodIDHashtable->buckets[key];

    if(bucket<=0) {

        bucket = methodIDHashtable->buckets[key];
        if(bucket<=0) {
            bucket = calloc(1, sizeof(MethodIDBucket));
            if(bucket<=0) {
                error("Unable to allocate MethodIDBucket\n");
                unlock(&methodIDHashtable->lock, false);
                return;
            }

            if(!compareAndSwapPtrBool((volatile uintptr_t *)&methodIDHashtable->buckets[key], NULL, bucket)) {
                error("Bucket was supposed to be NULL!\n");
            }
        }

    }

    lock(&bucket->lock, false);

    MethodIDNode *newNode = calloc(1, sizeof(MethodIDNode));
    if(newNode<=0) {
        error("Unable to allocateMethodIDNode\n");
        unlock(&bucket->lock, false);
        unlock(&methodIDHashtable->lock, false);
        return;
    }

    newNode->jvmtiMethodID = jvmtiMethodID;
    newNode->jvmtiClass = jvmtiClass;
    newNode->classID = classID;
    newNode->methodID = methodID;
    newNode->allocationType = SINGLE_ALLOCATION;

    if(!compareAndSwapPtrBool((volatile uintptr_t *)&bucket->rootNode, NULL, newNode)) {

        volatile MethodIDNode *node = bucket->rootNode;

        while(!compareAndSwapPtrBool((volatile uintptr_t *)&node->next, NULL, newNode)) {
            node = node->next;
        }

    } else {
    }

    bucket->chainLength++;

    unlock(&bucket->lock, false);
    unlock(&methodIDHashtable->lock, false);

}


void addToClassHashtable(ClassNode *classNode) {

    uint32_t hashCode = jenkins_one_at_a_time_hash((char*)classNode->name, strlen((const char*)classNode->name));
    uint32_t key = hashCode & CLASS_HASHTABLE_MASK;

    lock(&classHashtable->lock, true);

    debug("Adding %s to class hashtable on %d\n", JVMStringToPlatform(classNode->name));

    classNode->hashCode = hashCode;

    ClassBucket *bucket = classHashtable->buckets[key];

    if(bucket<=0) {

        bucket = calloc(1, sizeof(ClassBucket));
        if(bucket<=0) {
            error("Unable to allocate ClassBucket\n");
            unlock(&classHashtable->lock, true);
            return;
        }

        if(!compareAndSwapPtrBool(( uintptr_t *)&classHashtable->buckets[key], NULL, bucket)) {
            free(bucket);
            bucket = classHashtable->buckets[key];
        }

    }

    if(!compareAndSwapPtrBool(( uintptr_t *)&bucket->rootNode, NULL, classNode)) {

        ClassNode *node = bucket->rootNode;

        while(!compareAndSwapPtrBool(( uintptr_t *)&node->next, NULL, classNode)) {
            node = node->next;
        }

    }

    bucket->chainLength++;

    unlock(&classHashtable->lock, true);

}

void addToThreadHashtable(ThreadNode *threadNode) {

    uint64_t hashCode = hashUint64((uint64_t) threadNode->threadID);
    uint32_t key = hashCode & THREAD_HASHTABLE_MASK;

    lock(&threadHashtable->lock, false);

    ThreadBucket *bucket = threadHashtable->buckets[key];

    if(bucket<=0) {

        bucket = threadHashtable->buckets[key];
        if(bucket<=0) {
            bucket = calloc(1, sizeof(ThreadBucket));
            if(bucket<=0) {
                error("Unable to allocate ThreadBucket\n");
                unlock(&threadHashtable->lock, false);
                return;
            }
            threadHashtable->buckets[key] = bucket;
        }
        lock(&bucket->lock, false);

    } else {
        lock(&bucket->lock, false);
    }

    {
        ThreadNode *node = bucket->rootNode;
        while(node>0) {

            if(node->threadID == threadNode->threadID) {
                unlock(&bucket->lock, false);
                unlock(&threadHashtable->lock, false);
                return;
            }

            node = node->next;
        }
    }


    ThreadNode *node = bucket->rootNode;

    if(node<=0) {
        bucket->rootNode = threadNode;
    } else {
        while(node->next>0) {
            node = node->next;
        }
        node->next = threadNode;
    }
    bucket->chainLength++;
    unlock(&threadHashtable->lock, false);
    unlock(&bucket->lock, false);
}




void clearMethodIDHashtable() {


    for(int i=0;i <METHOD_ID_HASHTABLE_BUCKETS; i++) {


        if(methodIDHashtable->buckets[i] != NULL) {


            MethodIDNode *node = methodIDHashtable->buckets[i]->rootNode;

            while(node!=NULL) {


                MethodIDNode *tempNode = node;
                node = node->next;

                if(tempNode->allocationType==LIST_ALLOCATION || tempNode->allocationType==SINGLE_ALLOCATION) {
                    free(tempNode);
                }

            }

            free(methodIDHashtable->buckets[i]);

        }

    }

    free(methodIDHashtable);
    createMethodIDHashtable();

}


void clearThreadHashtable() {

    for(int i=0;i <THREAD_HASHTABLE_BUCKETS; i++) {

        if(threadHashtable->buckets[i] != NULL) {

            ThreadNode *node = threadHashtable->buckets[i]->rootNode;

            while(node!=NULL) {

                if(node->name) free(node->name);
                if(node->threadBuffer) free(node->threadBuffer);
                //if(node->methodCache) free(node->methodCache);


                ThreadNode *tempNode = node;
                node = node->next;
                free(tempNode);

            }

            free(threadHashtable->buckets[i]);
        }
    }

    free(threadHashtable);
    createThreadHashtable();
}

void clearClassHashtable() {

    for(int i=0;i <CLASS_HASHTABLE_BUCKETS; i++) {

        if(classHashtable->buckets[i] != NULL) {

            ClassNode *node = classHashtable->buckets[i]->rootNode;

            while(node!=NULL) {

                if(node->name) free(node->name);

                if(node->profilerName) free(node->profilerName);

                if(node->methods) {
                    for(int j=0;j<node->numberOfMethods;j++){
                        if(node->methods[j].name) free(node->methods[j].name);
                        if(node->methods[j].signature) free(node->methods[j].signature);
                        if(node->methods[j].generic) free(node->methods[j].generic);
                    }

                    free(node->methods);
                }


                if(node->fields) {
                    for(int j=0;j<node->numberOfFields;j++){
                        if(node->fields[j].name) free(node->fields[j].name);
                        if(node->fields[j].signature) free(node->fields[j].signature);
                        if(node->fields[j].generic) free(node->fields[j].generic);
                    }

                    free(node->fields);
                }


                if(node->interfaces) free(node->interfaces);


                ClassNode *tempNode = node;
                node = node->next;
                free(tempNode);


            }

            free(classHashtable->buckets[i]);

        }

    }

    free(classHashtable);
    createClassHashtable();

}



void reportStatistics() {


    uint32_t numberOfUsedBuckets = 0;
    uint32_t longestChain = 0;
    uint32_t totalEntries = 0;

    for(int i=0;i <METHOD_ID_HASHTABLE_BUCKETS; i++) {
        if(methodIDHashtable->buckets[i] != NULL) {
            numberOfUsedBuckets++;
            totalEntries += methodIDHashtable->buckets[i]->chainLength;
            if(methodIDHashtable->buckets[i]->chainLength > longestChain) {
                longestChain = methodIDHashtable->buckets[i]->chainLength;
            }
        }
    }

    info("MethodIDHashTable:\n");
    info("\tNumber of Used Buckets: %d\n", numberOfUsedBuckets);
    info("\tTotal Entries: %d\n", totalEntries);
    info("\tLongest Chain: %d\n", longestChain);
    info("\n");


    numberOfUsedBuckets = 0;
    longestChain = 0;
    totalEntries = 0;

    for(int i=0;i <CLASS_HASHTABLE_BUCKETS; i++) {
        if(classHashtable->buckets[i] != NULL) {
            numberOfUsedBuckets++;
            totalEntries += classHashtable->buckets[i]->chainLength;
            if(classHashtable->buckets[i]->chainLength > longestChain) {
                longestChain = classHashtable->buckets[i]->chainLength;
            }
        }
    }

    info("ClassHashTable:\n");
    info("\tNumber of Used Buckets: %d\n", numberOfUsedBuckets);
    info("\tTotal Entries: %d\n", totalEntries);
    info("\tLongest Chain: %d\n", longestChain);
    info("\tLockedFor: %" PRIu64 "\n", classHashtable->lockedFor);
    info("\n");


    numberOfUsedBuckets = 0;
    longestChain = 0;
    totalEntries = 0;

    for(int i=0;i <THREAD_HASHTABLE_BUCKETS; i++) {
        if(threadHashtable->buckets[i] != NULL) {
            numberOfUsedBuckets++;
            totalEntries += threadHashtable->buckets[i]->chainLength;
            if(threadHashtable->buckets[i]->chainLength > longestChain) {
                longestChain = threadHashtable->buckets[i]->chainLength;
            }
        }
    }

    info("ThreadHashTable:\n");
    info("\tNumber of Used Buckets: %d\n", numberOfUsedBuckets);
    info("\tTotal Entries: %d\n", totalEntries);
    info("\tLongest Chain: %d\n", longestChain);
    info("\n");


    numberOfUsedBuckets = 0;
    longestChain = 0;
    totalEntries = 0;


}
