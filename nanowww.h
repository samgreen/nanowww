/*
 * Copyright (c) 2009, tokuhiro matsuno
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef NANOWWW_H_
#define NANOWWW_H_

/**
 * Copyright (C) 2009 tokuhirom
 * modified BSD License.
 */

/**

=head1 SYNOPSIS

    #include "nanowww.h"
    nanowww::Client www;
    nanowww::Response;
    assert(www.send_get(&res, "http://google.com");
    printf("%s\n", res.content());

=head1 POLICY

=head2 WILL SUPPORTS

=head3 important thing

set content from FILE* fh for streaming upload.

=head3 not important thing

basic auth

https support

=head2 WILL NOT SUPPORTS

=over 4

=item I/O multiplexing request

use thread, instead.

=back

=head2 MAY NOT SUPPORTS

I don't need it.But, if you write the patch, I'll merge it.

    KEEP ALIVE

    win32 port

    support proxy-env


*/

#include <nanosocket/nanosocket.h>
#include <picouri/picouri.h>
#include <picohttpparser/picohttpparser.h>
#include <picoalarm/picoalarm.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <cstring>
#include <cassert>
#include <string>
#include <map>
#include <iostream>
#include <sstream>

#define NANOWWW_VERSION "0.01"
#define NANOWWW_USER_AGENT "NanoWWW/" NANOWWW_VERSION

#define NANOWWW_MAX_HEADERS 64
#define NANOWWW_READ_BUFFER_SIZE 60*1024

namespace nanowww {

    class Headers {
    private:
        std::map<std::string, std::string> _map;
    public:
        void set_header(const char *key, const char *val) {
            _map[key] = val;
        }
        std::string get_header(const char *key) {
            return _map[key];
        }
        std::string as_string() {
            std::map<std::string, std::string>::iterator iter;
            std::string res;
            for ( iter = _map.begin(); iter != _map.end(); ++iter ) {
                assert(
                    iter->second.find('\n') == std::string::npos
                    && iter->second.find('\r') == std::string::npos
                );
                res += iter->first + ": " + iter->second + "\r\n";
            }
            return res;
        }
    };

    class Response {
    private:
        int status_;
        std::string msg_;
        Headers hdr_;
        std::string content_;
    public:
        Response() {
            status_ = -1;
        }
        bool is_success() {
            return status_ == 200;
        }
        int status() { return status_; }
        void set_status(int _status) {
            status_ = _status;
        }
        std::string message() { return msg_; }
        void set_message(const char *str, size_t len) {
            msg_.assign(str, len);
        }
        Headers * headers() { return &hdr_; }
        void set_header(const std::string &key, const std::string &val) {
            hdr_.set_header(key.c_str(), val.c_str());
        }
        std::string get_header(const char *key) {
            return hdr_.get_header(key);
        }
        void add_content(const std::string &src) {
            content_.append(src);
        }
        void add_content(const char *src, size_t len) {
            content_.append(src, len);
        }
        std::string content() { return content_; }
    };

    class Uri {
    private:
        char * uri_;
        std::string host_;
        int port_;
        std::string path_query_;
    public:
        Uri(const char*src) {
            uri_ = strdup(src);
            assert(uri_);
            const char * scheme;
            size_t scheme_len;
            const char * _host;
            size_t host_len;
            const char *_path_query;
            int path_query_len;
            int ret = pu_parse_uri(uri_, strlen(uri_), &scheme, &scheme_len, &_host, &host_len, &port_, &_path_query, &path_query_len);
            assert(ret == 0);  // TODO: throw
            host_.assign(_host, host_len);
            path_query_.assign(_path_query, path_query_len);
        }
        ~Uri() {
            if (uri_) { free(uri_); }
        }
        std::string host() { return host_; }
        int port() { return port_; }
        std::string path_query() { return path_query_; }
    };

    class Request {
    private:
        Headers headers_;
        std::string method_;
        std::string content_;
        Uri *uri_;
    public:
        Request(const char *method, const char *uri, const char *content) {
            this->Init(method, uri, content);
        }
        Request(const char *method, const char *uri, std::map<std::string, std::string> &post) {
            std::string content;
            std::map<std::string, std::string>::iterator iter = post.begin();
            for (; iter!=post.end(); ++iter) {
                if (!content.empty()) { content += "&"; }
                std::string key = iter->first;
                std::string val = iter->second;
                content += pu_escape_uri(key) + "=" + pu_escape_uri(val);
            }
            this->set_header("Content-Type", "application/x-www-form-urlencoded");

            this->Init(method, uri, content.c_str());
        }
        ~Request() {
            if (uri_) { delete uri_; }
        }
        void set_header(const char* key, const char *val) {
            this->headers_.set_header(key, val);
        }
        Headers *headers() { return &headers_; }
        Uri *uri() { return uri_; }
        void set_uri(const char *uri) { delete uri_; uri_ = new Uri(uri); }
        void set_uri(const std::string &uri) { this->set_uri(uri.c_str()); }
        std::string method() { return method_; }
        std::string content() { return content_; }
    protected:
        void Init(const char *method, const char *uri, const char *content) {
            method_  = method;
            content_ = content;
            uri_     = new Uri(uri);
            assert(uri_);
            this->set_header("User-Agent", NANOWWW_USER_AGENT);
            this->set_header("Host", uri_->host().c_str());

            // TODO: do not use sstream
            std::stringstream s;
            s << content_.size();
            this->set_header("Content-Length", s.str().c_str());
        }
    };

    class Client {
    private:
        std::string errstr_;
        unsigned int timeout_;
        int max_redirects_;
    public:
        Client() {
            timeout_ = 60; // default timeout is 60sec
            max_redirects_ = 7; // default. same as LWP::UA
        }
        /**
         * @args tiemout: timeout in sec.
         * @return none
         */
        void set_timeout(unsigned int timeout) {
            timeout_ = timeout;
        }
        unsigned int timeout() { return timeout_; }
        /**
         * @return string of latest error
         */
        std::string errstr() { return errstr_; }
        int send_get(Response *res, const std::string &uri) {
            return this->send_get(res, uri.c_str());
        }
        int send_get(Response *res, const char *uri) {
            Request req("GET", uri, "");
            return this->send_request(req, res);
        }
        int send_post(Response *res, const char *uri, std::map<std::string, std::string> &data) {
            Request req("POST", uri, data);
            return this->send_request(req, res);
        }
        int send_post(Response *res, const char *uri, const char *content) {
            Request req("POST", uri, content);
            return this->send_request(req, res);
        }
        int send_put(Response *res, const char *uri, const char *content) {
            Request req("PUT", uri, content);
            return this->send_request(req, res);
        }
        int send_delete(Response *res, const char *uri) {
            Request req("DELETE", uri, "");
            return this->send_request(req, res);
        }
        /**
         * @return return true if success
         */
        bool send_request(Request &req, Response *res) {
            return send_request_internal(req, res, this->max_redirects_);
        }
    protected:
        bool send_request_internal(Request &req, Response *res, int remain_redirect) {
            picoalarm::Alarm alrm(this->timeout_); // RAII
            
            short port = req.uri()->port() == 0 ? 80 : req.uri()->port();
            nanosocket::Socket sock;
            if (!sock.connect(req.uri()->host().c_str(), port)) {
                errstr_ = sock.errstr();
                return false;
            }
            int opt = 1;
            sock.setsockopt(IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int));

            std::string hbuf =
                  req.method() + " " + req.uri()->path_query() + " HTTP/1.0\r\n"
                + req.headers()->as_string()
                + "\r\n"
            ;

            if (sock.send(hbuf.c_str(), hbuf.size()) != (int)hbuf.size()) {
                errstr_ = "error in writing header";
                return false;
            }
            if (sock.send(req.content().c_str(), req.content().size()) != (int)req.content().size()) {
                errstr_ = "error in writing body";
                return false;
            }

            // reading loop
            std::string buf;
            char read_buf[NANOWWW_READ_BUFFER_SIZE];

            // read header part
            while (1) {
                int nread = sock.recv(read_buf, sizeof(read_buf));
                if (nread == 0) { // eof
                    errstr_ = "EOF";
                    return false;
                }
                if (nread < 0) { // error
                    errstr_ = strerror(errno);
                    return false;
                }
                buf.append(read_buf, nread);

                int minor_version;
                int status;
                const char *msg;
                size_t msg_len;
                struct phr_header headers[NANOWWW_MAX_HEADERS];
                size_t num_headers = sizeof(headers) / sizeof(headers[0]);
                int last_len = 0;
                int ret = phr_parse_response(buf.c_str(), buf.size(), &minor_version, &status, &msg, &msg_len, headers, &num_headers, last_len);
                if (ret > 0) {
                    res->set_status(status);
                    res->set_message(msg, msg_len);
                    for (size_t i=0; i<num_headers; i++) {
                        res->set_header(
                            std::string(headers[i].name, headers[i].name_len),
                            std::string(headers[i].value, headers[i].value_len)
                        );
                    }
                    res->add_content(buf.substr(ret));
                    break;
                } else if (ret == -1) { // parse error
                    errstr_ = "http response parse error";
                    return false;
                } else if (ret == -2) { // request is partial
                    continue;
                }
            }

            if ((res->status() == 301 || res->status() == 302) && (req.method() == std::string("GET") || req.method() == std::string("POST"))) {
                if (remain_redirect <= 0) {
                    errstr_ = "Redirect loop detected";
                    return false;
                } else {
                    req.set_uri(res->get_header("Location"));
                    return this->send_request_internal(req, res, remain_redirect-1);
                }
            }

            // read body part
            while (1) {
                int nread = sock.recv(read_buf, sizeof(read_buf));
                if (nread == 0) { // eof
                    break;
                } else if (nread < 0) { // error
                    errstr_ = strerror(errno);
                    return false;
                } else {
                    res->add_content(read_buf, nread);
                    continue;
                }
            }

            sock.close();
            return true;
        }
        int max_redirects() { return max_redirects_; }
        void set_max_redirects(int mr) { max_redirects_ = mr; }
    };
};

#endif  // NANOWWW_H_
