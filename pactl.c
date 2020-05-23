/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <locale.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>

#include <sndfile.h>

#include <pulse/pulseaudio.h>
#include <pulse/ext-device-restore.h>

static void quit(int);
double active_sink_vol = 0.0f;
char active_sink_port[1000];

typedef struct node {
    char node_id[1000]; // sink-index, or sink-input-index
    bool is_sink;
    uint64_t idx;
    double vol;
    char *node_desc;
    FILE *logfile;
    struct node *next;
} node;

node* ll_head = NULL;

node* new_node(const char* node_id, const char *fname, uint64_t idx, bool is_sink);
void log_node(node *n);
void remove_node(node *n);
node* get_node_with_id(const char *node_id);
void update_vol_with_node_id(const char *node_id, double vol);
void append_to_list(node *n);
void remove_from_list(const char *node_id);
void cleanup_all_nodes();

void remove_node(node *n) {
    // mark vol = 0 for this node
    n->vol = -1.0f;
    log_node(n);

    if (!fclose(n->logfile))
        printf("Closed file for node# %s\n", n->node_id);
    else {
        printf("Failed to close file for node# %s", n->node_id);
        quit(1);
    }
    free(n);
}

node* new_node(const char* node_id, const char *fname, uint64_t idx, bool is_sink) {
    node* n;

    n = (node*) malloc(sizeof(node));
    strcpy(n->node_id, node_id);
    n->vol = 0.0f;
    n->is_sink = is_sink;
    n->idx = idx;
    n->next = NULL;

    printf("Creating a new node id %s, fname %s\n", node_id, fname);

    n->logfile = fopen(fname, "a+");
    if (!n->logfile) {
        printf("Failed to open file %s for node_id %s. \n", fname, node_id);
        quit(1);
    }
    return n;
}

void log_node(node *n) {
    // assumes that the file stream is opened at node creation and
    // has not been closed
    fprintf(n->logfile, "%lu,%lu,%s,%0.6f,%0.6f\n",
            (unsigned long)time(NULL),
            n->idx,
            active_sink_port,
            n->vol,
            (n->is_sink) ? n->vol : n->vol * active_sink_vol
        );
    fflush(n->logfile);
}

void log_all_nodes() {
    node *cur = ll_head;
    while (cur != NULL) {
        log_node(cur);
        cur = cur->next;
    }
}

void append_to_list(node* n) {
    n->next = ll_head;
    ll_head = n;
}

void remove_from_list(const char* node_id) {
    node *cur, *prev;
    cur = ll_head;

    if (cur == NULL) {
        printf("Failed to remove from an empty list.\n");
        return;
    }

    prev = cur;
    while (cur != NULL) {
        if (strcmp(cur->node_id, node_id) == 0)
            break;
        prev = cur;
        cur = cur->next;
    }

    if (cur == NULL) {
        printf("Failed to remove node which doesn't exist.\n");
        return;
    }

    // got the item at cur
    // prev == cur : the first node
    if (prev == cur) {
        ll_head = cur->next;
    }
    prev->next = cur->next;
    remove_node(cur);
}

node* get_node_with_id(const char* node_id) {
    node *cur = ll_head;
    while (cur != NULL) {
        if (strcmp(node_id, cur->node_id) == 0) {
            return cur;
        }
        cur = cur->next;
    }
    return cur;
}

void update_vol_with_node_id(const char* node_id, double vol) {
    node *n = get_node_with_id(node_id);
    if (n == NULL) {
        printf("Node with id %s not found.\n", node_id);
        return;
    }
    n->vol = vol;
    log_node(n);
}

void cleanup_all_nodes() {
    node *cur = ll_head;
    while (cur != NULL) {
        log_node(cur);
        node *temp = cur;
        cur = cur->next;
        remove_node(temp);
    }
}


static pa_context *context = NULL;
static pa_mainloop_api *mainloop_api = NULL;

static char
    *list_type = NULL,
    *sample_name = NULL,
    *sink_name = NULL,
    *source_name = NULL,
    *module_args = NULL,
    *card_name = NULL,
    *profile_name = NULL,
    *port_name = NULL,
    *formats = NULL;

static pa_proplist *proplist = NULL;

static SNDFILE *sndfile = NULL;
static pa_stream *sample_stream = NULL;

/* This variable tracks the number of ongoing asynchronous operations. When a
 * new operation begins, this is incremented simply with actions++, and when
 * an operation finishes, this is decremented with the complete_action()
 * function, which shuts down the program if actions reaches zero. */
static int actions = 0;

static void quit(int ret) {
    assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}

static void context_drain_complete(pa_context *c, void *userdata) {
    (void)userdata;
    pa_context_disconnect(c);
}

static void drain(void) {
    pa_operation *o;

    if (!(o = pa_context_drain(context, context_drain_complete, NULL)))
        pa_context_disconnect(context);
    else
        pa_operation_unref(o);
}

static void complete_action(void) {
    assert(actions > 0);

    if (!(--actions))
        drain();
}


static void get_sink_info_callback(pa_context *c, const pa_sink_info *i, int is_last, void *userdata) {
    (void)userdata;
    uint64_t vol_perc;
    double vol;

    if (is_last < 0) {
        fprintf(stderr, "Failed to get sink information: %s\n", pa_strerror(pa_context_errno(c)));
        /* quit(1); */
        return;
    }

    if (is_last) {
        complete_action();
        return;
    }

    assert(i);

    vol_perc = ((uint64_t)pa_cvolume_avg(&i->volume) * 100 + (uint64_t)PA_VOLUME_NORM / 2) / (uint64_t)PA_VOLUME_NORM;
    vol = (i->mute || i->state != PA_SINK_RUNNING) ? 0.0f : vol_perc / 100.00f;


    if(!(i->active_port))
    {
        // Port not active
        return;
    }

    char fname[1000], node_id[1000];
    sprintf(node_id, "sink-%u-%s", i->index, i->active_port->name);
    sprintf(fname, "sink-%s.log", i->active_port->name);

    if (i->state != PA_SINK_IDLE) {
        strcpy(active_sink_port, i->active_port->name);
        active_sink_vol = vol;
    }

    printf("%s %s %lf\n", node_id, fname, vol);

    if (get_node_with_id(node_id) == NULL) {
        printf("creating node with id %s\n", node_id);
        node *n = new_node(node_id, fname, i->index, true);
        append_to_list(n);
    }

    update_vol_with_node_id(node_id, vol);
    log_all_nodes();
}


static void get_sink_input_info_callback(pa_context *c, const pa_sink_input_info *i, int is_last, void *userdata) {
    (void)userdata;
    uint64_t vol_perc;
    double vol;
    const char *application_process_binary;


    if (is_last < 0) {
        fprintf(stderr, "Failed to get sink input information: %s\n", pa_strerror(pa_context_errno(c)));
        /* quit(1); */
        return;
    }

    if (is_last) {
        complete_action();
        return;
    }

    assert(i);

    vol_perc = ((uint64_t)pa_cvolume_avg(&i->volume) * 100 + (uint64_t)PA_VOLUME_NORM / 2) / (uint64_t)PA_VOLUME_NORM;

    vol = (i->corked || i->mute) ? 0.0f : vol_perc / 100.00f;

    application_process_binary = pa_proplist_gets(i->proplist, "application.process.binary");


    // here we form a node and insert it to the list
    char fname[1000], node_id[1000];
    sprintf(node_id, "sink_input-%u", i->index);
    sprintf(fname, "sink_input-%s.log", application_process_binary);

    printf("%s %s %lf\n", node_id, fname, vol);

    if (get_node_with_id(node_id) == NULL) {
        printf("creating node with id %s\n", node_id);
        node *n = new_node(node_id, fname, i->index, false);
        append_to_list(n);
    }

    update_vol_with_node_id(node_id, vol);
}



/* PA_MAX_FORMATS is defined in internal.h so we just define a sane value here */
#define MAX_FORMATS 256


static void context_subscribe_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    (void)userdata;
    assert(c);
    pa_operation *o = NULL;

    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {

    case PA_SUBSCRIPTION_EVENT_SINK:
        if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) !=  PA_SUBSCRIPTION_EVENT_REMOVE)
            o = pa_context_get_sink_info_by_index(c, idx, get_sink_info_callback, NULL);
        else {
            // this doesn't happen on my machine, so no worries for now
            log_all_nodes();
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
        if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) !=  PA_SUBSCRIPTION_EVENT_REMOVE) {
            o = pa_context_get_sink_input_info(c, idx, get_sink_input_info_callback, NULL);
        } else {
            char node_id[1000];
            sprintf(node_id, "sink_input-%u", idx);
            printf("removing from list nodeid %s\n", node_id);
            remove_from_list(node_id);
        }
        break;
    }

    if (o) {
        pa_operation_unref(o);
        actions++;
    }

    fflush(stdout);
}

static void context_state_callback(pa_context *c, void *userdata) {
    (void)userdata;
    pa_operation *o = NULL;

    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY:
            // first subscribe to sink and sink_input events
            pa_context_set_subscribe_callback(c, context_subscribe_callback, NULL);
            o = pa_context_subscribe(c,
                                    PA_SUBSCRIPTION_MASK_SINK|
                                    PA_SUBSCRIPTION_MASK_SINK_INPUT,
                                    NULL,
                                    NULL);
            if (o) {
                pa_operation_unref(o);
                actions++;
            }

            // second list current sinks
            o = pa_context_get_sink_info_list(c, get_sink_info_callback, NULL);
            if (o) {
                pa_operation_unref(o);
                actions++;
            }

            // third list current sink_inputs
            o = pa_context_get_sink_input_info_list(c, get_sink_input_info_callback, NULL);
            if (o) {
                pa_operation_unref(o);
                actions++;
            }

            if (actions < 3) {
                fprintf(stderr, "Operation failed: %s\n", pa_strerror(pa_context_errno(c)));
                quit(1);
            }

            break;

        case PA_CONTEXT_TERMINATED:
            quit(0);
            break;

        case PA_CONTEXT_FAILED:
        default:
            fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
            quit(1);
    }
}

static void exit_signal_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata) {
    (void)m;
    (void)e;
    (void)sig;
    (void)userdata;
    fprintf(stderr, "Got SIGINT, exiting.\n");
    cleanup_all_nodes();
    quit(0);
}

enum {
    ARG_VERSION = 256
};

int main() {
    fprintf(stdout, "Hi!\n");
    pa_mainloop *m = NULL;
    int ret = 1;
    char *server = NULL;

    setlocale(LC_ALL, "");
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PULSE_LOCALEDIR);
#endif

    proplist = pa_proplist_new();

    if (!(m = pa_mainloop_new())) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        goto quit;
    }

    mainloop_api = pa_mainloop_get_api(m);

    assert(pa_signal_init(mainloop_api) == 0);
    pa_signal_new(SIGINT, exit_signal_callback, NULL);
    pa_signal_new(SIGTERM, exit_signal_callback, NULL);

    /** Ingnore SIGPIPE **/
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));

    if (sigaction(SIGPIPE, NULL, &sa) < 0) {
        fprintf(stderr, "sigaction(): %s\n", strerror(errno));
        return 1;
    }

    sa.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        fprintf(stderr, "sigaction(): %s\n", strerror(errno));
        return 1;
    }
    /****/

    if (!(context = pa_context_new_with_proplist(mainloop_api, NULL, proplist))) {
        fprintf(stderr, "pa_context_new() failed.\n");
        goto quit;
    }

    pa_context_set_state_callback(context, context_state_callback, NULL);
    if (pa_context_connect(context, server, 0, NULL) < 0) {
        fprintf(stderr, "pa_context_connect() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto quit;
    }

    if (pa_mainloop_run(m, &ret) < 0) {
        fprintf(stderr, "pa_mainloop_run() failed.\n");
        goto quit;
    }

// cleanup routine
quit:
    if (sample_stream)
        pa_stream_unref(sample_stream);

    if (context)
        pa_context_unref(context);

    if (m) {
        pa_signal_done();
        pa_mainloop_free(m);
    }

    pa_xfree(server);
    pa_xfree(list_type);
    pa_xfree(sample_name);
    pa_xfree(sink_name);
    pa_xfree(source_name);
    pa_xfree(module_args);
    pa_xfree(card_name);
    pa_xfree(profile_name);
    pa_xfree(port_name);
    pa_xfree(formats);

    if (sndfile)
        sf_close(sndfile);

    if (proplist)
        pa_proplist_free(proplist);

    return ret;
}
