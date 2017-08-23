
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>

#include "ngx_http_lua_config.h"
#include "api/ngx_http_lua_api.h"


static int ngx_http_lua_config_prefix(lua_State *L);
static int ngx_http_lua_config_configure(lua_State *L);
static int ngx_http_lua_ngx_get_conf(lua_State *L);

void
ngx_http_lua_inject_config_api(lua_State *L)
{
    /* ngx.config */

    lua_createtable(L, 0, 6 /* nrec */);    /* .config */

#if (NGX_DEBUG)
    lua_pushboolean(L, 1);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "debug");

    lua_pushcfunction(L, ngx_http_lua_config_prefix);
    lua_setfield(L, -2, "prefix");

    lua_pushinteger(L, nginx_version);
    lua_setfield(L, -2, "nginx_version");

    lua_pushinteger(L, ngx_http_lua_version);
    lua_setfield(L, -2, "ngx_lua_version");

    lua_pushcfunction(L, ngx_http_lua_ngx_get_conf);
    lua_setfield(L, -2, "get_conf");
    
    lua_pushcfunction(L, ngx_http_lua_config_configure);
    lua_setfield(L, -2, "nginx_configure");

    lua_pushliteral(L, "http");
    lua_setfield(L, -2, "subsystem");

    lua_setfield(L, -2, "config");
}


static int
ngx_http_lua_config_prefix(lua_State *L)
{
    lua_pushlstring(L, (char *) ngx_cycle->prefix.data,
                    ngx_cycle->prefix.len);
    return 1;
}


static int
ngx_http_lua_config_configure(lua_State *L)
{
    lua_pushliteral(L, NGX_CONFIGURE);
    return 1;
}

static void
ngx_http_lua_conf_get_static_locations(yajl_gen g, ngx_http_location_tree_node_t *node)
{
    if ( node == NULL ) {
        return;
    }

    yajl_gen_string(g, (unsigned char *)node->name, node->len);

    if ( node->left ) {
        return ngx_http_lua_conf_get_static_locations(g, node->left);
    }

    if ( node->right ) {
        return ngx_http_lua_conf_get_static_locations(g, node->right);
    }

}

static void
ngx_http_lua_conf_get_locations(yajl_gen g, ngx_http_core_loc_conf_t *clcf)
{
    ngx_http_core_loc_conf_t                    **clcfp;

    if ( clcf ==  NULL ) {
        return;
    }

    yajl_gen_string(g, (unsigned char *) LOCATIONS_KEY, ngx_strlen(LOCATIONS_KEY));

    yajl_gen_array_open(g);

    ngx_http_lua_conf_get_static_locations(g, clcf->static_locations);

    if ( clcf->regex_locations == NULL ) {
        goto array_close;
    }

    for (clcfp = clcf->regex_locations; *clcfp; clcfp++) {
        yajl_gen_string(g, (unsigned char *)(*clcfp)->name.data , (*clcfp)->name.len);
    }

array_close:
    yajl_gen_array_close(g);

}

static void
ngx_http_lua_conf_get_servers(yajl_gen g, ngx_http_core_srv_conf_t *server,
        ngx_http_conf_addr_t *addr)
{
    size_t                                      n;
    ngx_http_server_name_t                      *name;
    ngx_http_core_loc_conf_t                    *clcf;


    yajl_gen_map_open(g);
    name = server->server_names.elts;

    //server_name
    yajl_gen_string(g, (unsigned char *)SERVER_NAME_KEY, ngx_strlen(SERVER_NAME_KEY));
    yajl_gen_array_open(g);

    for (n = 0; n < server->server_names.nelts; n++) {
        yajl_gen_string(g, (unsigned char *)name[n].name.data, name[n].name.len);
    }

    yajl_gen_array_close(g);

    //server_port
    yajl_gen_string(g, (unsigned char *) SERVER_PORT_KEY, strlen(SERVER_PORT_KEY));
    yajl_gen_string(g, (unsigned char *) addr->opt.addr, ngx_strlen(addr->opt.addr));

    clcf = server->ctx->loc_conf[ngx_http_core_module.ctx_index];

    ngx_http_lua_conf_get_locations(g, clcf);

    yajl_gen_map_close(g);
}



/*
{
    "http":{
        "servers":[
            {
                "server_name":"www.didichuxing.com",
                "port":80,
                "locations":[
                        "location":"/index",
                        "location":"/home"
                ]
            }
        ]
    }
}
*/

static int
ngx_http_lua_ngx_get_conf(lua_State *L)
{
    size_t                                      s, i, j;
    size_t                                      len;
    ngx_http_conf_addr_t                        *addr;
    ngx_http_conf_port_t                        *port;
    const unsigned char                         *buf;
    ngx_http_core_main_conf_t                   *cmcf;
    ngx_http_core_srv_conf_t                    **server;
    yajl_gen                                    g;
    yajl_gen_status                             status;


    g = yajl_gen_alloc(NULL);
    if ( g == NULL ) {
        lua_pushnil(L);
        return 1;
    }

    yajl_gen_config(g, yajl_gen_beautify, 0);

    yajl_gen_map_open(g);

    //http
    yajl_gen_string(g, (unsigned char *)HTTP_KEY, strlen(HTTP_KEY));

    yajl_gen_map_open(g);

    //servers
    yajl_gen_string(g, (unsigned char *)SERVERS_KEY, ngx_strlen(SERVERS_KEY));

    yajl_gen_array_open(g);

    cmcf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_core_module);
    //cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    port = cmcf->ports->elts;
    for (s = 0; s < cmcf->ports->nelts; s++) {

        addr = port[s].addrs.elts;
        for (i = 0; i < port[s].addrs.nelts; i++) {

            server = addr[i].servers.elts;
            for (j = 0; j < addr[i].servers.nelts; j++) {

                ngx_http_lua_conf_get_servers(g, server[j], &addr[i]);
            }

        }
    }

    yajl_gen_array_close(g);

    yajl_gen_map_close(g);

    yajl_gen_map_close(g);

    status = yajl_gen_get_buf(g, &buf, &len);
    if(status != yajl_gen_status_ok) {
        yajl_gen_free(g);
        lua_pushnil(L);

        return 1;
    }

    lua_pushlstring(L, (char *) buf, len);

    return 1;
}

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
