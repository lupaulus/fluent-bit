/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2020 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef FLB_OUTPUT_H
#define FLB_OUTPUT_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <monkey/mk_core.h>
#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_bits.h>
#include <fluent-bit/flb_io.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_network.h>
#include <fluent-bit/flb_engine.h>
#include <fluent-bit/flb_task.h>
#include <fluent-bit/flb_coro.h>
#include <fluent-bit/flb_callback.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/tls/flb_tls.h>

#ifdef FLB_HAVE_REGEX
#include <fluent-bit/flb_regex.h>
#endif

/* Output plugin masks */
#define FLB_OUTPUT_NET           32  /* output address may set host and port */
#define FLB_OUTPUT_PLUGIN_CORE    0
#define FLB_OUTPUT_PLUGIN_PROXY   1
#define FLB_OUTPUT_NO_MULTIPLEX 512

/*
 * Tests callbacks
 * ===============
 */
struct flb_test_out_formatter {
    /*
     * Runtime Library Mode
     * ====================
     * When the runtime library enable the test formatter mode, it needs to
     * keep a reference of the context and other information:
     *
     * - rt_ctx : context created by flb_create()
     *
     * - rt_ffd : this plugin assigned 'integer' created by flb_output()
     *
     * - rt_step_calback: intermediary function to receive the results of
     *                    the formatter plugin test function.
     *
     * - rt_data: opaque data type for rt_step_callback()
     */

    /* runtime library context */
    void *rt_ctx;

    /* runtime library: assigned plugin integer */
    int rt_ffd;

    /*
     * "runtime step callback": this function pointer is used by Fluent Bit
     * library mode to reference a test function that must retrieve the
     * results of 'callback'. Consider this an intermediary function to
     * transfer the results to the runtime test.
     *
     * This function is private and should not be set manually in the plugin
     * code, it's set on src/flb_lib.c .
     */
    void (*rt_out_callback) (void *, int, int, void *, size_t, void *);

    /*
     * opaque data type passed by the runtime library to be used on
     * rt_step_test().
     */
    void *rt_data;

    /* optional context for flush callback */
    void *flush_ctx;

    /*
     * Callback
     * =========
     * "Formatter callback": it references the plugin function that performs
     * data formatting (msgpack -> local data). This entry is mostly to
     * expose the plugin local function.
     */
    int (*callback) (/* Fluent Bit context */
                     struct flb_config *,
                     /* plugin that ingested the records */
                     struct flb_input_instance *,
                     void *,       /* plugin instance context */
                     void *,       /* optional flush context */
                     const char *, /* tag        */
                     int,          /* tag length */
                     const void *, /* incoming msgpack data */
                     size_t,       /* incoming msgpack size */
                     void **,      /* output buffer      */
                     size_t *);    /* output buffer size */
};

struct flb_output_plugin {
    /*
     * The type defines if this is a core-based plugin or it's handled by
     * some specific proxy.
     */
    int type;
    void *proxy;

    int flags;

    /* The plugin name */
    char *name;

    /* Plugin description */
    char *description;

    struct flb_config_map *config_map;

    /*
     * Output network info:
     *
     * An output plugin can be specified just using it shortname or using the
     * complete network address format, e.g:
     *
     *  $ fluent-bit -i cpu -o plugin://hostname:port/uri
     *
     * where:
     *
     *   plugin   = the output plugin shortname
     *   name     = IP address or hostname of the target
     *   port     = target TCP port
     *   uri      = extra information that may be used by the plugin
     */
    struct flb_net_host host;

    /* Initalization */
    int (*cb_init)    (struct flb_output_instance *, struct flb_config *, void *);

    /* Pre run */
    int (*cb_pre_run) (void *, struct flb_config *);

    /* Flush callback */
    void (*cb_flush) (const void *, size_t,
                      const char *, int,
                      struct flb_input_instance *,
                      void *,
                      struct flb_config *);

    /* Exit */
    int (*cb_exit) (void *, struct flb_config *);

    /* Tests */
    struct flb_test_out_formatter test_formatter;

    /* Link to global list from flb_config->outputs */
    struct mk_list _head;
};

/*
 * Each initialized plugin must have an instance, same plugin may be
 * loaded more than one time.
 *
 * An instance try to contain plugin data separating what is fixed data
 * and the variable one that is generated when the plugin is invoked.
 */
struct flb_output_instance {
    struct mk_event event;               /* events handler               */
    uint64_t mask_id;                    /* internal bitmask for routing */
    int id;                              /* instance id                  */
    int log_level;                       /* instance log level           */
    char name[32];                       /* numbered name (cpu -> cpu.0) */
    char *alias;                         /* alias name for the instance  */
    int flags;                           /* inherit flags from plugin    */
    int test_mode;                       /* running tests? (default:off) */
    flb_pipefd_t ch_events[2];           /* channel for events           */
    struct flb_output_plugin *p;         /* original plugin              */
    void *context;                       /* plugin configuration context */


    /* Plugin properties */
    int retry_limit;                     /* max of retries allowed       */
    int use_tls;                         /* bool, try to use TLS for I/O */
    char *match;                         /* match rule for tag/routing   */
#ifdef FLB_HAVE_REGEX
    struct flb_regex *match_regex;       /* match rule (regex) based on Tags */
#endif

#ifdef FLB_HAVE_TLS
    int tls_verify;                      /* Verify certs (default: true) */
    int tls_debug;                       /* mbedtls debug level          */
    char *tls_vhost;                     /* Virtual hostname for SNI     */
    char *tls_ca_path;                   /* Path to certificates         */
    char *tls_ca_file;                   /* CA root cert                 */
    char *tls_crt_file;                  /* Certificate                  */
    char *tls_key_file;                  /* Cert Key                     */
    char *tls_key_passwd;                /* Cert Key Password            */
#endif

    /*
     * network info:
     *
     * An input plugin can be specified just using it shortname or using the
     * complete network address format, e.g:
     *
     *  $ fluent-bit -i cpu -o plugin://hostname:port/uri
     *
     * where:
     *
     *   plugin   = the output plugin shortname
     *   name     = IP address or hostname of the target
     *   port     = target TCP port
     *   uri      = extra information that may be used by the plugin
     */
    struct flb_net_host host;

    /*
     * Optional data passed to the plugin, this info is useful when
     * running Fluent Bit in library mode and the target plugin needs
     * some specific data from it caller.
     */
    void *data;

    /* Output handler configuration */
    void *out_context;

    /*
     * The threads_queue is the head for the linked list that holds co-routines
     * nodes information that needs to be processed.
     */
    struct mk_list th_queue;

#ifdef FLB_HAVE_TLS
    struct flb_tls *tls;
#else
    void *tls;
#endif

    /*
     * configuration properties: incoming properties set by the caller. This
     * list is what the instance received by either a configuration file or
     * through the command line arguments. This list is validated by the
     * plugin.
     */
    struct mk_list properties;

    /*
     * configuration map: a new API is landing on Fluent Bit v1.4 that allows
     * plugins to specify at registration time the allowed configuration
     * properties and it data types. Config map is an optional API for now
     * and some plugins will take advantage of it. When the API is used, the
     * config map will validate the configuration, set default values
     * and merge the 'properties' (above) into the map.
     */
    struct mk_list *config_map;

    /* General network options like timeouts and keepalive */
    struct flb_net_setup net_setup;
    struct mk_list *net_config_map;
    struct mk_list net_properties;

    struct mk_list _head;                /* link to config->inputs       */

#ifdef FLB_HAVE_METRICS
    struct flb_metrics *metrics;         /* metrics                      */
#endif

    /* Callbacks context */
    struct flb_callback *callback;

    /* Tests */
    struct flb_test_out_formatter test_formatter;

    /*
     * Buffer counter: it counts the total of disk space (filesystem) used by buffers
     */
    size_t fs_chunks_size;

    /*
     * Buffer limit: optional limit set by configuration so this output instance
     * cannot buffer more than total_limit_size (bytes unit).
     *
     * Note that this is the limit set to the filesystem buffer mechanism so the
     * input instance routered to this output plugin should configure to use
     * filesystem as buffer type.
     */
    size_t total_limit_size;

    /* Thread Pool: this is optional for the caller */
    int tp_workers;
    struct flb_tp *tp;

    struct mk_list upstreams;

    /* Keep a reference to the original context this instance belongs to */
    struct flb_config *config;
};

struct flb_output_coro {
    int id;                            /* out-thread ID      */
    const void *buffer;                /* output buffer      */
    struct flb_task *task;             /* Parent flb_task    */
    struct flb_config *config;         /* FLB context        */
    struct flb_output_instance *o_ins; /* output instance    */
    struct flb_coro *parent;           /* parent thread addr */
    struct mk_list _head_output;       /* Link to flb_output_instance->th_queue */
    struct mk_list _head;              /* Link to flb_task->threads */

};

static FLB_INLINE
struct flb_output_coro *flb_output_coro_get(int id, struct flb_task *task)
{
    struct mk_list *head;
    struct flb_output_coro *out_coro = NULL;

    mk_list_foreach(head, &task->coros) {
        out_coro = mk_list_entry(head, struct flb_output_coro, _head);
        if (out_coro->id == id) {
            return out_coro;
        }
    }

    return NULL;
}

static FLB_INLINE int flb_output_coro_destroy_id(int id, struct flb_task *task)
{
    struct flb_output_coro *out_coro;
    struct flb_coro *coro;

    out_coro = flb_output_coro_get(id, task);
    if (!out_coro) {
        return -1;
    }

    mk_list_del(&out_coro->_head_output);
    mk_list_del(&out_coro->_head);
    coro = out_coro->parent;

    flb_coro_destroy(coro);
    task->users--;

    return 0;
}

/* When an output_thread is going to be destroyed, this callback is triggered */
static FLB_INLINE void cb_output_coro_destroy(void *data)
{
    struct flb_output_coro *out_coro;

    out_coro = (struct flb_output_coro *) data;

    flb_debug("[out thread] cb_destroy thread_id=%i", out_coro->id);

    out_coro->task->users--;
    mk_list_del(&out_coro->_head_output);
    mk_list_del(&out_coro->_head);
}

/*
 * libco do not support parameters in the entrypoint function due to the
 * complexity of implementation in terms of architecture and compiler, but
 * it provide a workaround using a global structure as a middle entry-point
 * that achieve the same stuff.
 */
struct flb_libco_out_params {
    const void  *data;
    size_t bytes;
    const char *tag;
    int tag_len;
    struct flb_input_instance *i_ins;
    void *out_context;
    struct flb_config *config;
    struct flb_output_plugin *out_plugin;
    struct flb_coro *th;
};

extern FLB_TLS_DEFINE(struct flb_libco_out_params, flb_libco_params);

static FLB_INLINE void output_params_set(struct flb_coro *th,
                              const void *data, size_t bytes,
                              const char *tag, int tag_len,
                              struct flb_input_instance *i_ins,
                              struct flb_output_plugin *out_plugin,
                              void *out_context, struct flb_config *config)
{
    struct flb_libco_out_params *params;

    params = (struct flb_libco_out_params *) FLB_TLS_GET(flb_libco_params);
    if (!params) {
        params = (struct flb_libco_out_params *)
            flb_malloc(sizeof(struct flb_libco_out_params));
        if (!params) {
            flb_errno();
            return;
        }
    }

    /* Callback parameters in order */
    params->data        = data;
    params->bytes       = bytes;
    params->tag         = tag;
    params->tag_len     = tag_len;
    params->i_ins       = i_ins;
    params->out_context = out_context;
    params->config      = config;
    params->out_plugin  = out_plugin;
    params->th          = th;

    FLB_TLS_SET(flb_libco_params, params);
    co_switch(th->callee);
}

static FLB_INLINE void output_pre_cb_flush(void)
{
    const void *data;
    size_t bytes;
    const char *tag;
    int tag_len;
    struct flb_input_instance *i_ins;
    struct flb_output_plugin *out_p;
    void *out_context;
    struct flb_config *config;
    struct flb_coro *th;
    struct flb_libco_out_params *params;

    params = (struct flb_libco_out_params *) FLB_TLS_GET(flb_libco_params);
    if (!params) {
        flb_error("[output] no co-routines params defined, unexpected");
        return;
    }

    data        = params->data;
    bytes       = params->bytes;
    tag         = params->tag;
    tag_len     = params->tag_len;
    i_ins       = params->i_ins;
    out_p       = params->out_plugin;
    out_context = params->out_context;
    config      = params->config;
    th          = params->th;

    /*
     * Until this point the th->callee already set the variables, so we
     * wait until the core wanted to resume so we really trigger the
     * output callback.
     */
    co_switch(th->caller);

    /* Continue, we will resume later */
    out_p->cb_flush(data, bytes, tag, tag_len, i_ins, out_context, config);
}

static FLB_INLINE
struct flb_coro *flb_output_thread(struct flb_task *task,
                                   struct flb_input_instance *i_ins,
                                   struct flb_output_instance *o_ins,
                                   struct flb_config *config,
                                   const void *buf, size_t size,
                                   const char *tag, int tag_len)
{
    size_t stack_size;
    struct flb_output_coro *out_coro;
    struct flb_coro *coro;

    /* Create a new thread */
    coro = flb_coro_new(sizeof(struct flb_output_coro),
                        cb_output_coro_destroy);
    if (!coro) {
        return NULL;
    }

    /* Custom output-thread info */
    out_coro = (struct flb_output_coro *) FLB_CORO_DATA(coro);
    if (!out_coro) {
        flb_errno();
        return NULL;
    }

    /*
     * Each 'Thread' receives an 'id'. This is assigned when this thread
     * is linked into the parent Task by flb_task_add_thread(...). The
     * 'id' is always incremental.
     */
    out_coro->id      = 0;
    out_coro->o_ins   = o_ins;
    out_coro->task    = task;
    out_coro->buffer  = buf;
    out_coro->config  = config;
    out_coro->parent  = coro;

    coro->caller = co_active();
    coro->callee = co_create(config->coro_stack_size,
                             output_pre_cb_flush, &stack_size);

#ifdef FLB_HAVE_VALGRIND
    coro->valgrind_stack_id = \
        VALGRIND_STACK_REGISTER(coro->callee, ((char *) coro->callee) + stack_size);
#endif

    mk_list_add(&out_coro->_head_output, &o_ins->th_queue);

    /* Workaround for makecontext() */
    output_params_set(coro,
                      buf,
                      size,
                      tag,
                      tag_len,
                      i_ins,
                      o_ins->p,
                      o_ins->context,
                      config);
    return coro;
}

/*
 * This function is used by the output plugins to return. It's mandatory
 * as it will take care to signal the event loop letting know the flush
 * callback has done.
 *
 * The signal emmited indicate the 'Task' number that have finished plus
 * a return value. The return value is either FLB_OK, FLB_RETRY or FLB_ERROR.
 */
static inline void flb_output_return(int ret, struct flb_coro *co) {
    int n;
    uint32_t set;
    uint64_t val;
    struct flb_task *task;
    struct flb_output_coro *out_coro;
#ifdef FLB_HAVE_METRICS
    int records;
#endif

    out_coro = (struct flb_output_coro *) FLB_CORO_DATA(co);
    task = out_coro->task;

    /*
     * To compose the signal event the relevant info is:
     *
     * - Unique Task events id: 2 in this case
     * - Return value: FLB_OK (0) or FLB_ERROR (1)
     * - Task ID
     *
     * We put together the return value with the task_id on the 32 bits at right
     */
    set = FLB_TASK_SET(ret, task->id, out_coro->id);
    val = FLB_BITS_U64_SET(2 /* FLB_ENGINE_TASK */, set);

    /* Notify the main event loop about our return status */
    n = flb_pipe_w(out_coro->o_ins->ch_events[1], (void *) &val, sizeof(val));
    if (n == -1) {
        flb_errno();
    }

#ifdef FLB_HAVE_METRICS
    if (out_coro->o_ins->metrics) {
        if (ret == FLB_OK) {
            records = task->records;
            flb_metrics_sum(FLB_METRIC_OUT_OK_RECORDS, records,
                            out_coro->o_ins->metrics);
            flb_metrics_sum(FLB_METRIC_OUT_OK_BYTES, task->size,
                            out_coro->o_ins->metrics);
        }
        else if (ret == FLB_ERROR) {
            flb_metrics_sum(FLB_METRIC_OUT_ERROR, 1, out_coro->o_ins->metrics);
        }
        else if (ret == FLB_RETRY) {
            /*
             * Counting retries is happening in the event loop/scheduler side
             * since it also needs to count if some retry fails to re-schedule.
             */
        }
    }
#endif
}

static inline void flb_output_return_do(int x)
{
    struct flb_coro *co;

    co = (struct flb_coro *) pthread_getspecific(flb_coro_key);
    flb_output_return(x, co);
    /*
     * Each co-routine handler have different ways to handle a return,
     * just use the wrapper.
     */
    flb_coro_yield(co, FLB_TRUE);
}

#define FLB_OUTPUT_RETURN(x)                                            \
    flb_output_return_do(x);                                            \
    return

static inline int flb_output_config_map_set(struct flb_output_instance *ins,
                                            void *context)
{
    int ret;

    /* Process normal properties */
    ret = flb_config_map_set(&ins->properties, ins->config_map, context);
    if (ret == -1) {
        return -1;
    }

    /* Net properties */
    if (ins->net_config_map) {
        ret = flb_config_map_set(&ins->net_properties, ins->net_config_map,
                                 &ins->net_setup);
    }
    return ret;
}

struct flb_output_instance *flb_output_new(struct flb_config *config,
                                           const char *output, void *data);
const char *flb_output_name(struct flb_output_instance *in);
int flb_output_set_property(struct flb_output_instance *out,
                            const char *k, const char *v);
const char *flb_output_get_property(const char *key, struct flb_output_instance *ins);
void flb_output_net_default(const char *host, int port,
                            struct flb_output_instance *ins);
const char *flb_output_name(struct flb_output_instance *ins);
void flb_output_pre_run(struct flb_config *config);
void flb_output_exit(struct flb_config *config);
void flb_output_set_context(struct flb_output_instance *ins, void *context);
int flb_output_instance_destroy(struct flb_output_instance *ins);
int flb_output_init_all(struct flb_config *config);
int flb_output_check(struct flb_config *config);
int flb_output_upstream_set(struct flb_upstream *u, struct flb_output_instance *ins);
void flb_output_prepare();
int flb_output_set_http_debug_callbacks(struct flb_output_instance *ins);

int flb_output_task_flush(struct flb_task *task,
                          struct flb_output_instance *out_ins,
                          struct flb_config *config);

#endif
