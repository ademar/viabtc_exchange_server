/*
 * Description: 
 *     History: yang@haipo.me, 2017/04/21, create
 */

# include "aw_config.h"
# include "aw_server.h"
# include "aw_auth.h"

static rpc_clt *listener;
static ws_svr *svr;
static nw_state *state_context;
static nw_cache *privdata_cache;

struct state_data {
    nw_ses      *ses;
    uint64_t    ses_id;
    uint64_t    request_id;
};

static int send_json(nw_ses *ses, const json_t *json)
{
    char *message_data = json_dumps(json, 0);
    if (message_data == NULL)
        return -__LINE__;
    log_trace("send to: %s message: %s", nw_sock_human_addr(&ses->peer_addr), message_data);
    int ret = ws_send_text(ses, message_data);
    free(message_data);
    return ret;
}

int send_error(nw_ses *ses, uint64_t id, int code, const char *message)
{
    json_t *error = json_object();
    json_object_set_new(error, "code", json_integer(code));
    json_object_set_new(error, "message", json_string(message));

    json_t *reply = json_object();
    json_object_set_new(reply, "error", error);
    json_object_set_new(reply, "result", json_null());
    json_object_set_new(reply, "id", json_integer(id));

    int ret = send_json(ses, reply);
    json_decref(reply);

    return ret;
}

int send_error_invalid_argument(nw_ses *ses, uint64_t id)
{
    return send_error(ses, id, 1, "invalid argument");
}

int send_error_internal_error(nw_ses *ses, uint64_t id)
{
    return send_error(ses, id, 2, "internal error");
}

int send_error_service_unavailable(nw_ses *ses, uint64_t id)
{
    return send_error(ses, id, 3, "service unavailable");
}

int send_error_method_notfound(nw_ses *ses, uint64_t id)
{
    return send_error(ses, id, 4, "method not found");
}

int send_error_service_timeout(nw_ses *ses, uint64_t id)
{
    return send_error(ses, id, 5, "service timeout");
}

int send_result(nw_ses *ses, uint64_t id, json_t *result)
{
    json_t *reply = json_object();
    json_object_set_new(reply, "error", json_null());
    json_object_set_new(reply, "result", result);
    json_object_set_new(reply, "id", json_integer(id));

    int ret = send_json(ses, reply);
    json_decref(reply);

    return ret;
}

int send_success(nw_ses *ses, uint64_t id)
{
    json_t *result = json_object();
    json_object_set_new(result, "status", json_string("success"));
    return send_result(ses, id, result);
}

int send_notify(nw_ses *ses, const char *method, json_t *params)
{
    json_t *notify = json_object();
    json_object_set_new(notify, "method", json_string("method"));
    json_object_set_new(notify, "params", params);
    json_object_set_new(notify, "id", json_null());

    int ret = send_json(ses, notify);
    json_decref(notify);

    return ret;
}

static int on_method_server_time(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return send_result(ses, id, json_integer(time(NULL)));
}

static int on_method_server_auth(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return send_auth_request(ses, id, info, params);
}

static int on_method_kline_query(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_kline_subscribe(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_depth_query(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_depth_subscribe(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_order_put_limit(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_order_put_market(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_order_query(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_order_subscribe(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_asset_query(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_method_asset_subscribe(nw_ses *ses, uint64_t id, struct clt_info *info, json_t *params)
{
    return 0;
}

static int on_message(nw_ses *ses, const char *remote, const char *url, void *message, size_t size)
{
    struct clt_info *info = ws_ses_privdata(ses);
    log_trace("new websocket message from: %"PRIu64":%s, url: %s, size: %zu", ses->id, remote, url, size);
    json_t *msg = json_loadb(message, size, 0, NULL);
    if (msg == NULL) {
        goto decode_error;
    }

    json_t *id = json_object_get(msg, "id");
    if (!id || !json_is_integer(id)) {
        goto decode_error;
    }
    json_t *method = json_object_get(msg, "method");
    if (!method || !json_is_string(method)) {
        goto decode_error;
    }
    json_t *params = json_object_get(msg, "params");
    if (!params || !json_is_array(params)) {
        goto decode_error;
    }

    sds _msg = sdsnewlen(message, size);
    log_trace("remote: %"PRIu64":%s message: %s", ses->id, remote, _msg);

    int ret;
    uint64_t _id = json_integer_value(id);
    const char *_method = json_string_value(method);
    if        (strcmp(_method, "server.time") == 0) {
        ret = on_method_server_time(ses, _id, info, params);
    } else if (strcmp(_method, "server.auth") == 0) {
        ret = on_method_server_auth(ses, _id, info, params);
    } else if (strcmp(_method, "kline.query") == 0) {
        ret = on_method_kline_query(ses, _id, info, params);
    } else if (strcmp(_method, "kline.subscribe") == 0) {
        ret = on_method_kline_subscribe(ses, _id, info, params);
    } else if (strcmp(_method, "depth.query") == 0) {
        ret = on_method_depth_query(ses, _id, info, params);
    } else if (strcmp(_method, "depth.subscribe") == 0) {
        ret = on_method_depth_subscribe(ses, _id, info, params);
    } else if (strcmp(_method, "order.put_limit") == 0) {
        ret = on_method_order_put_limit(ses, _id, info, params);
    } else if (strcmp(_method, "order.put_market") == 0) {
        ret = on_method_order_put_market(ses, _id, info, params);
    } else if (strcmp(_method, "order.query") == 0) {
        ret = on_method_order_query(ses, _id, info, params);
    } else if (strcmp(_method, "order.subscribe") == 0) {
        ret = on_method_order_subscribe(ses, _id, info, params);
    } else if (strcmp(_method, "asset.query") == 0) {
        ret = on_method_asset_query(ses, _id, info, params);
    } else if (strcmp(_method, "asset.subscribe") == 0) {
        ret = on_method_asset_subscribe(ses, _id, info, params);
    } else {
        log_error("remote: %"PRIu64":%s, unknown method, request: %s", ses->id, remote, _msg);
    }

    if (ret < 0) {
        log_error("remote: %"PRIu64":%s, request fail: %d, request: %s", ses->id, remote, ret, _msg);
        return -__LINE__;
    }

    sdsfree(_msg);
    json_decref(msg);

    return 0;

decode_error:
    if (msg)
        json_decref(msg);
    sds hex = hexdump(message, size);
    log_error("remote: %"PRIu64":%s, decode request fail, request body: \n%s", ses->id, remote, hex);
    sdsfree(hex);
    return -__LINE__;
}

static void on_upgrade(nw_ses *ses, const char *remote)
{
    log_trace("remote: %"PRIu64":%s upgrade to websocket", ses->id, remote);
    struct clt_info *info = ws_ses_privdata(ses);
    memset(info, 0, sizeof(struct clt_info));
}

static void on_close(nw_ses *ses, const char *remote)
{
    log_trace("remote: %"PRIu64":%s websocket connection close", ses->id, remote);
}

static void *on_privdata_alloc(void *svr)
{
    return nw_cache_alloc(privdata_cache);
}

static void on_privdata_free(void *svr, void *privdata)
{
    struct clt_info *info = privdata;
    if (info->taker_fee)
        mpd_del(info->taker_fee);
    if (info->maker_fee)
        mpd_del(info->maker_fee);
    nw_cache_free(privdata_cache, privdata);
}

static int init_websocket_svr(void)
{
    ws_svr_type type;
    memset(&type, 0, sizeof(type));
    type.on_upgrade = on_upgrade;
    type.on_close = on_close;
    type.on_message = on_message;
    type.on_privdata_alloc = on_privdata_alloc;
    type.on_privdata_free = on_privdata_free;

    svr = ws_svr_create(&settings.svr, &type);
    if (svr == NULL)
        return -__LINE__;

    privdata_cache = nw_cache_create(sizeof(struct clt_info));
    if (privdata_cache == NULL)
        return -__LINE__;

    return 0;
}

static void on_timeout(nw_state_entry *entry)
{
    struct state_data *state = entry->data;
    if (state->ses->id == state->ses_id)
        send_error_service_timeout(state->ses, state->request_id);
}

static int init_state(void)
{
    nw_state_type st;
    memset(&st, 0, sizeof(st));
    st.on_timeout = on_timeout;

    state_context = nw_state_create(&st, sizeof(struct state_data));
    if (state_context == NULL)
        return -__LINE__;

    return 0;
}

static void on_listener_connect(nw_ses *ses, bool result)
{
    if (result) {
        log_info("connect listener success");
    } else {
        log_info("connect listener fail");
    }
}

static void on_listener_recv_pkg(nw_ses *ses, rpc_pkg *pkg)
{
    return;
}

static void on_listener_recv_fd(nw_ses *ses, int fd)
{
    if (nw_svr_add_clt_fd(svr->raw_svr, fd) < 0) {
        log_error("nw_svr_add_clt_fd: %d fail: %s", fd, strerror(errno));
        close(fd);
    }
}

static int init_listener_clt(void)
{
    rpc_clt_cfg cfg;
    nw_addr_t addr;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = strdup("listener");
    cfg.addr_count = 1;
    cfg.addr_arr = &addr;
    if (nw_sock_cfg_parse(AW_LISTENER_BIND, &addr, &cfg.sock_type) < 0)
        return -__LINE__;
    cfg.max_pkg_size = 1024;

    rpc_clt_type type;
    memset(&type, 0, sizeof(type));
    type.on_connect  = on_listener_connect;
    type.on_recv_pkg = on_listener_recv_pkg;
    type.on_recv_fd  = on_listener_recv_fd;

    listener = rpc_clt_create(&cfg, &type);
    if (listener == NULL)
        return -__LINE__;
    if (rpc_clt_start(listener) < 0)
        return -__LINE__;

    return 0;
}

int init_server(void)
{
    ERR_RET(init_websocket_svr());
    ERR_RET(init_state());
    ERR_RET(init_listener_clt());

    return 0;
}

