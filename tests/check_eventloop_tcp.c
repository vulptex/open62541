/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <open62541/plugin/eventloop.h>
#include <open62541/plugin/log_stdout.h>
#include "open62541/types.h"
#include "open62541/types_generated.h"

#include "testing_clock.h"
#include <time.h>
#include <check.h>

#define N_EVENTS 10000

UA_EventLoop *el;

static void noopCallback(UA_ConnectionManager *cm, uintptr_t connectionId,
                         void **connectionContext, UA_StatusCode status,
                         size_t paramsSize, const UA_KeyValuePair *params,
                         UA_ByteString msg) {}

START_TEST(listenTCP) {
    el = UA_EventLoop_new_POSIX(UA_Log_Stdout);

    UA_UInt16 port = 4840;
    UA_Variant portVar;
    UA_Variant_setScalar(&portVar, &port, &UA_TYPES[UA_TYPES_UINT16]);
    UA_ConnectionManager *cm = UA_ConnectionManager_new_POSIX_TCP(UA_STRING("tcpCM"));
    cm->connectionCallback = noopCallback;
    UA_KeyValueMap_set(&cm->eventSource.params,
                       &cm->eventSource.paramsSize,
                       UA_QUALIFIEDNAME(0, "listen-port"), &portVar);
    el->registerEventSource(el, &cm->eventSource);

    el->start(el);

    for(size_t i = 0; i < 10; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    int max_stop_iteration_count = 1000;
    int iteration = 0;
    /* Stop the EventLoop */
    el->stop(el);
    while(el->state != UA_EVENTLOOPSTATE_STOPPED && iteration < max_stop_iteration_count) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
        iteration++;
    }
    el->free(el);
    el = NULL;
} END_TEST

static unsigned connCount;
static char *testMsg = "open62541";
static uintptr_t clientId;
static UA_Boolean received;
static void
connectionCallback(UA_ConnectionManager *cm, uintptr_t connectionId,
                   void **connectionContext, UA_StatusCode status,
                   size_t paramsSize, const UA_KeyValuePair *params,
                   UA_ByteString msg) {
    if(*connectionContext != NULL)
        clientId = connectionId;
    if(msg.length == 0 && status == UA_STATUSCODE_GOOD) {
        connCount++;

        /* The remote-hostname is set during the first callback */
        if(paramsSize > 0) {
            const void *hn =
                UA_KeyValueMap_getScalar(params, paramsSize,
                                         UA_QUALIFIEDNAME(0, "remote-hostname"),
                                         &UA_TYPES[UA_TYPES_STRING]);
            ck_assert(hn != NULL);
        }
    }
    if(status != UA_STATUSCODE_GOOD) {
        connCount--;
    }

    if(msg.length > 0) {
        UA_ByteString rcv = UA_BYTESTRING(testMsg);
        ck_assert(UA_String_equal(&msg, &rcv));
        received = true;
    }
}

static void
illegalConnectionCallback(UA_ConnectionManager *cm, uintptr_t connectionId,
                          void **connectionContext, UA_StatusCode status,
                          size_t paramsSize, const UA_KeyValuePair *params,
                          UA_ByteString msg) {
    UA_StatusCode rv = el->run(el, 1);
    ck_assert_uint_eq(rv, UA_STATUSCODE_BADINTERNALERROR);
    if(*connectionContext != NULL)
        clientId = connectionId;
    if(msg.length == 0 && status == UA_STATUSCODE_GOOD)
        connCount++;
    if(status != UA_STATUSCODE_GOOD)
        connCount--;
    if(msg.length > 0) {
        UA_ByteString rcv = UA_BYTESTRING(testMsg);
        ck_assert(UA_String_equal(&msg, &rcv));
        received = true;
    }
}

START_TEST(runEventloopFailsIfCalledFromCallback) {
    el = UA_EventLoop_new_POSIX(UA_Log_Stdout);

    UA_UInt16 port = 4840;
    UA_Variant portVar;
    UA_Variant_setScalar(&portVar, &port, &UA_TYPES[UA_TYPES_UINT16]);
    UA_ConnectionManager *cm = UA_ConnectionManager_new_POSIX_TCP(UA_STRING("tcpCM"));
    cm->connectionCallback = illegalConnectionCallback;
    UA_KeyValueMap_set(&cm->eventSource.params,
                       &cm->eventSource.paramsSize,
                       UA_QUALIFIEDNAME(0, "listen-port"), &portVar);
    el->registerEventSource(el, &cm->eventSource);

    connCount = 0;
    el->start(el);

    /* Open a client connection */
    clientId = 0;

    UA_String targetHost = UA_STRING("localhost");
    UA_KeyValuePair params[2];
    params[0].key = UA_QUALIFIEDNAME(0, "port");
    params[0].value = portVar;
    params[1].key = UA_QUALIFIEDNAME(0, "hostname");
    UA_Variant_setScalar(&params[1].value, &targetHost, &UA_TYPES[UA_TYPES_STRING]);

    UA_StatusCode retval = cm->openConnection(cm, 2, params, (void*)0x01);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    for(size_t i = 0; i < 10; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert(clientId != 0);
    ck_assert_uint_eq(connCount, 2);

    /* Send a message from the client */
    received = false;
    UA_ByteString snd;
    retval = cm->allocNetworkBuffer(cm, clientId, &snd, strlen(testMsg));
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    memcpy(snd.data, testMsg, strlen(testMsg));
    retval = cm->sendWithConnection(cm, clientId, 0, NULL, &snd);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    for(size_t i = 0; i < 10; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert(received);

    /* Close the connection */
    retval = cm->closeConnection(cm, clientId);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(connCount, 2);
    for(size_t i = 0; i < 10; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert_uint_eq(connCount, 0);

    int max_stop_iteration_count = 1000;
    int iteration = 0;
    /* Stop the EventLoop */
    el->stop(el);
    while(el->state != UA_EVENTLOOPSTATE_STOPPED &&
          iteration < max_stop_iteration_count) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
        iteration++;
    }
    el->free(el);
    el = NULL;
} END_TEST

START_TEST(connectTCP) {
    el = UA_EventLoop_new_POSIX(UA_Log_Stdout);

    UA_UInt16 port = 4840;
    UA_Variant portVar;
    UA_Variant_setScalar(&portVar, &port, &UA_TYPES[UA_TYPES_UINT16]);
    UA_ConnectionManager *cm = UA_ConnectionManager_new_POSIX_TCP(UA_STRING("tcpCM"));
    cm->connectionCallback = connectionCallback;
    UA_KeyValueMap_set(&cm->eventSource.params,
                       &cm->eventSource.paramsSize,
                       UA_QUALIFIEDNAME(0, "listen-port"), &portVar);
    el->registerEventSource(el, &cm->eventSource);

    connCount = 0;
    el->start(el);

    /* Open a client connection */
    clientId = 0;

    UA_String targetHost = UA_STRING("localhost");
    UA_KeyValuePair params[2];
    params[0].key = UA_QUALIFIEDNAME(0, "port");
    params[0].value = portVar;
    params[1].key = UA_QUALIFIEDNAME(0, "hostname");
    UA_Variant_setScalar(&params[1].value, &targetHost, &UA_TYPES[UA_TYPES_STRING]);

    UA_StatusCode retval = cm->openConnection(cm, 2, params, (void*)0x01);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    for(size_t i = 0; i < 2; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert(clientId != 0);
    ck_assert_uint_eq(connCount, 2);

    /* Send a message from the client */
    received = false;
    UA_ByteString snd;
    retval = cm->allocNetworkBuffer(cm, clientId, &snd, strlen(testMsg));
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    memcpy(snd.data, testMsg, strlen(testMsg));
    retval = cm->sendWithConnection(cm, clientId, 0, NULL, &snd);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    for(size_t i = 0; i < 2; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert(received);

    /* Close the connection */
    retval = cm->closeConnection(cm, clientId);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(connCount, 2);
    for(size_t i = 0; i < 2; i++) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
    }
    ck_assert_uint_eq(connCount, 0);

    /* Stop the EventLoop */
    int max_stop_iteration_count = 10;
    int iteration = 0;
    /* Stop the EventLoop */
    el->stop(el);
    while(el->state != UA_EVENTLOOPSTATE_STOPPED &&
          iteration < max_stop_iteration_count) {
        UA_DateTime next = el->run(el, 1);
        UA_fakeSleep((UA_UInt32)((next - UA_DateTime_now()) / UA_DATETIME_MSEC));
        iteration++;
    }
    ck_assert(el->state == UA_EVENTLOOPSTATE_STOPPED);
    el->free(el);
    el = NULL;
} END_TEST

int main(void) {
    Suite *s  = suite_create("Test TCP EventLoop");
    TCase *tc = tcase_create("test cases");
    tcase_add_test(tc, listenTCP);
    tcase_add_test(tc, connectTCP);
    tcase_add_test(tc, runEventloopFailsIfCalledFromCallback);
    suite_add_tcase(s, tc);

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all (sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
