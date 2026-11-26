/* hiredis without a server: the reply parser fed fixed RESP2 and RESP3 bytes,
 * its protocol error, command formatting with exact output bytes, and sds
 * strings.  The same text builds as C99 (main.c) and as C++. */
#include <hiredis/hiredis.h>
#include <hiredis/sds.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, int line, const char *what)
{
    if (!ok) {
        fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

/* Parses one complete reply from `wire`; the reader must hold nothing after. */
static redisReply *parse(const char *wire)
{
    redisReader *r = redisReaderCreate();
    void *reply = NULL;
    int rc;

    CHECK(r != NULL);
    CHECK(redisReaderFeed(r, wire, strlen(wire)) == REDIS_OK);
    rc = redisReaderGetReply(r, &reply);
    CHECK(rc == REDIS_OK);
    CHECK(reply != NULL);
    CHECK(r->err == 0);
    redisReaderFree(r);
    return (redisReply *)reply;
}

static int same(const char *got, size_t len, const char *want)
{
    return len == strlen(want) && memcmp(got, want, len) == 0;
}

static void test_scalars(void)
{
    redisReply *x;

    x = parse("+OK\r\n");
    CHECK(x->type == REDIS_REPLY_STATUS && same(x->str, x->len, "OK"));
    freeReplyObject(x);

    x = parse("-ERR unknown command\r\n");
    CHECK(x->type == REDIS_REPLY_ERROR && same(x->str, x->len, "ERR unknown command"));
    freeReplyObject(x);

    x = parse(":-42\r\n");
    CHECK(x->type == REDIS_REPLY_INTEGER && x->integer == -42);
    freeReplyObject(x);

    x = parse(":9223372036854775807\r\n");
    CHECK(x->type == REDIS_REPLY_INTEGER && x->integer == 9223372036854775807LL);
    freeReplyObject(x);

    x = parse("$12\r\nhello\r\nworld\r\n");
    CHECK(x->type == REDIS_REPLY_STRING && same(x->str, x->len, "hello\r\nworld"));
    freeReplyObject(x);

    x = parse("$0\r\n\r\n");
    CHECK(x->type == REDIS_REPLY_STRING && x->len == 0);
    freeReplyObject(x);

    x = parse("$-1\r\n");
    CHECK(x->type == REDIS_REPLY_NIL);
    freeReplyObject(x);
}

static void test_aggregates(void)
{
    redisReply *x;

    x = parse("*3\r\n$3\r\nfoo\r\n*2\r\n:7\r\n+ok\r\n$-1\r\n");
    CHECK(x->type == REDIS_REPLY_ARRAY && x->elements == 3);
    CHECK(x->element[0]->type == REDIS_REPLY_STRING && same(x->element[0]->str, x->element[0]->len, "foo"));
    CHECK(x->element[1]->type == REDIS_REPLY_ARRAY && x->element[1]->elements == 2);
    CHECK(x->element[1]->element[0]->type == REDIS_REPLY_INTEGER && x->element[1]->element[0]->integer == 7);
    CHECK(x->element[1]->element[1]->type == REDIS_REPLY_STATUS);
    CHECK(x->element[2]->type == REDIS_REPLY_NIL);
    freeReplyObject(x);

    x = parse("*0\r\n");
    CHECK(x->type == REDIS_REPLY_ARRAY && x->elements == 0);
    freeReplyObject(x);

    x = parse("*-1\r\n");
    CHECK(x->type == REDIS_REPLY_NIL);
    freeReplyObject(x);
}

static void test_resp3(void)
{
    redisReply *x;

    /* Doubles go through the vendored ffc parser. */
    x = parse(",3.14159\r\n");
    CHECK(x->type == REDIS_REPLY_DOUBLE && x->dval == 3.14159 && same(x->str, x->len, "3.14159"));
    freeReplyObject(x);

    x = parse(",-2.5e3\r\n");
    CHECK(x->type == REDIS_REPLY_DOUBLE && x->dval == -2500.0);
    freeReplyObject(x);

    x = parse(",inf\r\n");
    CHECK(x->type == REDIS_REPLY_DOUBLE && x->dval > 1e308);
    freeReplyObject(x);

    x = parse("#t\r\n");
    CHECK(x->type == REDIS_REPLY_BOOL && x->integer == 1);
    freeReplyObject(x);

    x = parse("#f\r\n");
    CHECK(x->type == REDIS_REPLY_BOOL && x->integer == 0);
    freeReplyObject(x);

    x = parse("_\r\n");
    CHECK(x->type == REDIS_REPLY_NIL);
    freeReplyObject(x);

    x = parse("%2\r\n+a\r\n:1\r\n+b\r\n:2\r\n");
    CHECK(x->type == REDIS_REPLY_MAP && x->elements == 4);
    CHECK(same(x->element[2]->str, x->element[2]->len, "b") && x->element[3]->integer == 2);
    freeReplyObject(x);

    x = parse("~2\r\n:1\r\n:2\r\n");
    CHECK(x->type == REDIS_REPLY_SET && x->elements == 2 && x->element[1]->integer == 2);
    freeReplyObject(x);

    x = parse(">2\r\n+message\r\n$2\r\nhi\r\n");
    CHECK(x->type == REDIS_REPLY_PUSH && redisIsPushReply(x) && x->elements == 2);
    freeReplyObject(x);

    x = parse("(123456789012345678901234567890\r\n");
    CHECK(x->type == REDIS_REPLY_BIGNUM && same(x->str, x->len, "123456789012345678901234567890"));
    freeReplyObject(x);

    x = parse("=15\r\ntxt:Some string\r\n");
    CHECK(x->type == REDIS_REPLY_VERB && strcmp(x->vtype, "txt") == 0 && same(x->str, x->len, "Some string"));
    freeReplyObject(x);
}

static void test_incremental_and_errors(void)
{
    static const char wire[] = "*2\r\n$3\r\nabc\r\n:5\r\n";
    redisReader *r = redisReaderCreate();
    redisReply *x = NULL;
    size_t i;

    /* One byte at a time: no reply until the last byte arrives. */
    for (i = 0; i + 1 < sizeof(wire); ++i) {
        void *reply = NULL;
        CHECK(redisReaderFeed(r, wire + i, 1) == REDIS_OK);
        CHECK(redisReaderGetReply(r, &reply) == REDIS_OK);
        if (i + 2 < sizeof(wire)) {
            CHECK(reply == NULL);
        } else {
            x = (redisReply *)reply;
        }
    }
    CHECK(x != NULL && x->type == REDIS_REPLY_ARRAY && x->elements == 2);
    CHECK(x != NULL && same(x->element[0]->str, x->element[0]->len, "abc") && x->element[1]->integer == 5);
    freeReplyObject(x);

    /* Two replies in one buffer come out one per call. */
    CHECK(redisReaderFeed(r, "+one\r\n+two\r\n", 12) == REDIS_OK);
    {
        void *a = NULL, *b = NULL, *c = NULL;
        CHECK(redisReaderGetReply(r, &a) == REDIS_OK && a != NULL);
        CHECK(redisReaderGetReply(r, &b) == REDIS_OK && b != NULL);
        CHECK(redisReaderGetReply(r, &c) == REDIS_OK && c == NULL);
        CHECK(same(((redisReply *)a)->str, ((redisReply *)a)->len, "one"));
        CHECK(same(((redisReply *)b)->str, ((redisReply *)b)->len, "two"));
        freeReplyObject(a);
        freeReplyObject(b);
    }
    redisReaderFree(r);

    /* A byte that starts no reply type is a protocol error. */
    r = redisReaderCreate();
    {
        void *reply = NULL;
        CHECK(redisReaderFeed(r, "@bogus\r\n", 8) == REDIS_OK);
        CHECK(redisReaderGetReply(r, &reply) == REDIS_ERR);
        CHECK(reply == NULL);
        CHECK(r->err == REDIS_ERR_PROTOCOL);
        CHECK(strcmp(r->errstr, "Protocol error, got \"@\" as reply type byte") == 0);
    }
    redisReaderFree(r);

    /* So is a malformed integer. */
    r = redisReaderCreate();
    {
        void *reply = NULL;
        CHECK(redisReaderFeed(r, ":12x\r\n", 6) == REDIS_OK);
        CHECK(redisReaderGetReply(r, &reply) == REDIS_ERR);
        CHECK(r->err == REDIS_ERR_PROTOCOL);
    }
    redisReaderFree(r);
}

static void test_format(void)
{
    char *cmd = NULL;
    int len;
    long long llen;
    const char *argv[3];
    size_t argvlen[3];

    len = redisFormatCommand(&cmd, "SET %s %b", "key", "va\0l", (size_t)4);
    CHECK(len == 32);
    CHECK(len == 32 && memcmp(cmd, "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$4\r\nva\0l\r\n", 32) == 0);
    redisFreeCommand(cmd);

    len = redisFormatCommand(&cmd, "INCRBY %s %d", "k", 10);
    CHECK(len > 0 && same(cmd, (size_t)len, "*3\r\n$6\r\nINCRBY\r\n$1\r\nk\r\n$2\r\n10\r\n"));
    redisFreeCommand(cmd);

    len = redisFormatCommand(&cmd, "GET %s", "a b");
    CHECK(len > 0 && same(cmd, (size_t)len, "*2\r\n$3\r\nGET\r\n$3\r\na b\r\n"));
    redisFreeCommand(cmd);

    argv[0] = "LPUSH";
    argv[1] = "list";
    argv[2] = "x\r\ny";
    argvlen[0] = 5;
    argvlen[1] = 4;
    argvlen[2] = 4;
    llen = redisFormatCommandArgv(&cmd, 3, argv, argvlen);
    CHECK(llen > 0 && same(cmd, (size_t)llen, "*3\r\n$5\r\nLPUSH\r\n$4\r\nlist\r\n$4\r\nx\r\ny\r\n"));
    redisFreeCommand(cmd);

    llen = redisFormatCommandArgv(&cmd, 2, argv, NULL);
    CHECK(llen > 0 && same(cmd, (size_t)llen, "*2\r\n$5\r\nLPUSH\r\n$4\r\nlist\r\n"));
    redisFreeCommand(cmd);
}

static void test_sds(void)
{
    sds s = sdsnew("hello");
    sds *parts;
    int count = 0;

    CHECK(sdslen(s) == 5 && strcmp(s, "hello") == 0);
    s = sdscatprintf(s, " %d-%s", 42, "x");
    CHECK(sdslen(s) == 10 && strcmp(s, "hello 42-x") == 0);
    s = sdstrim(s, "hx");
    CHECK(strcmp(s, "ello 42-") == 0);
    sdsfree(s);

    parts = sdssplitlen("a,b,,c", 6, ",", 1, &count);
    CHECK(parts != NULL && count == 4);
    if (parts != NULL && count == 4) {
        CHECK(strcmp(parts[0], "a") == 0 && strcmp(parts[1], "b") == 0);
        CHECK(sdslen(parts[2]) == 0 && strcmp(parts[3], "c") == 0);
    }
    sdsfreesplitres(parts, count);

    s = sdscatrepr(sdsnew(""), "a\n\"\x01", 4);
    CHECK(strcmp(s, "\"a\\n\\\"\\x01\"") == 0);
    sdsfree(s);
}

int main(void)
{
    test_scalars();
    test_aggregates();
    test_resp3();
    test_incremental_and_errors();
    test_format();
    test_sds();
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("hiredis OK");
    return 0;
}
