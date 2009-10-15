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
    if (www.send_get(&res, "http://google.com")) {
        if (res.is_success()) {
            cout << res.content() << endl;
        }
    } else {
        cerr << res.errstr() << endl;
    }

=head1 FAQ

=over 4

=item how to use I/O multiplexing request

use thread, instead.

=item how to use gopher/telnet/ftp.

I don't want to support gopher/telnet/ftp in nanowww.

=back

*/

#include <nanosocket/nanosocket.h>
#include <picouri/picouri.h>
#include <picohttpparser/picohttpparser.h>
#include <nanoalarm/nanoalarm.h>
#include <nanobase/nanobase.h>

#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <cstring>
#include <cassert>

#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <memory>

#define NANOWWW_VERSION "0.01"
#define NANOWWW_USER_AGENT "NanoWWW/" NANOWWW_VERSION

#define NANOWWW_MAX_HEADERS 64
#define NANOWWW_READ_BUFFER_SIZE 60*1024
#define NANOWWW_DEFAULT_MULTIPART_BUFFER_SIZE 60*1024

namespace nanowww {
    const char *version() {
        return NANOWWW_VERSION;
    }

    class Headers {
    private:
        std::map< std::string, std::vector<std::string> > headers_;
        typedef std::map< std::string, std::vector<std::string> >::iterator iterator;
    public:
        inline void push_header(const char *key, const char *val) {
            this->push_header(key, std::string(val));
        }
        inline void push_header(const char *key, const std::string &val) {
            iterator iter = headers_.find(key);
            if (iter != headers_.end()) {
                iter->second.push_back(val);
            } else {
                std::vector<std::string> v;
                v.push_back(val);
                headers_[std::string(key)] = v;
            }
        }
        inline void remove_header(const char *key) {
            iterator iter = headers_.find(key);
            if (iter != headers_.end()) {
                headers_.erase(iter);
            }
        }
        inline void set_header(const char *key, int val) {
            char * buf = new char[val/10+2];
            sprintf(buf, "%d", val);
            this->set_header(key, buf);
            delete [] buf;
        }
        inline void set_header(const char *key, const std::string &val) {
            this->remove_header(key);
            this->push_header(key, val);
        }
        inline void set_header(const char *key, const char *val) {
            this->set_header(key, std::string(val));
        }
        inline std::string get_header(const char *key) {
            iterator iter = headers_.find(key);
            if (iter != headers_.end()) {
                return iter->second[0];
            }
            return NULL;
        }
        inline std::string as_string() {
            std::string res;
            for ( iterator iter = headers_.begin(); iter != headers_.end(); ++iter ) {
                std::vector<std::string>::iterator ci = iter->second.begin();
                for (;ci!=iter->second.end(); ++ci) {
                    assert(
                           ci->find('\n') == std::string::npos
                        && ci->find('\r') == std::string::npos
                    );
                    res += iter->first + ": " + (*ci) + "\r\n";
                }
            }
            return res;
        }
        /**
         * username must not contains ':'
         */
        void set_authorization_basic(const std::string &username, const std::string &password) {
            this->_basic_auth("Authorization", username, password);
        }
    protected:
        void _basic_auth(const char *header, const std::string &username, const std::string &password) {
            assert(username.find(':') == std::string::npos);
            std::string val = username + ":" + password;
            unsigned char * buf = new unsigned char[nb_base64_needed_encoded_length(val.size())];
            nb_base64_encode((const unsigned char*)val.c_str(), val.size(), (unsigned char*)buf);
            this->set_header(header, std::string("Basic ") + ((const char*)buf));
            delete [] buf;
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
        inline bool is_success() {
            return status_ == 200;
        }
        inline int status() { return status_; }
        inline void set_status(int _status) {
            status_ = _status;
        }
        inline std::string message() { return msg_; }
        inline void set_message(const char *str, size_t len) {
            msg_.assign(str, len);
        }
        inline Headers * headers() { return &hdr_; }
        inline void push_header(const std::string &key, const std::string &val) {
            hdr_.push_header(key.c_str(), val.c_str());
        }
        inline std::string get_header(const char *key) {
            return hdr_.get_header(key);
        }
        inline void add_content(const std::string &src) {
            content_.append(src);
        }
        inline void add_content(const char *src, size_t len) {
            content_.append(src, len);
        }
        std::string content() { return content_; }
    };

    class Uri {
    private:
        char * uri_;
        std::string host_;
        std::string scheme_;
        int port_;
        std::string path_query_;
    public:
        Uri() {
            uri_ = NULL;
        }
        /**
         * @return true if valid url
         */
        bool parse(const std::string &src) {
            return this->parse(src.c_str());
        }
        bool parse(const char*src) {
            if (uri_) { free(uri_); }
            uri_ = strdup(src);
            assert(uri_);
            const char * scheme;
            size_t scheme_len;
            const char * _host;
            size_t host_len;
            const char *_path_query;
            int path_query_len;
            int ret = pu_parse_uri(uri_, strlen(uri_), &scheme, &scheme_len, &_host, &host_len, &port_, &_path_query, &path_query_len);
            if (ret != 0) {
                return false; // parse error
            }
            host_.assign(_host, host_len);
            path_query_.assign(_path_query, path_query_len);
            scheme_.assign(scheme, scheme_len);
            return true;
        }
        ~Uri() {
            if (uri_) { free(uri_); }
        }
        inline std::string host() { return host_; }
        inline std::string scheme() { return scheme_; }
        inline int port() { return port_; }
        inline std::string path_query() { return path_query_; }
        inline std::string as_string() { return std::string(uri_); }
        operator bool() const {
            return this->uri_;
        }
    };

    class Request {
    private:
        std::string content_;
    protected:
        Headers headers_;
        std::string method_;
        Uri uri_;
        size_t content_length_;
    public:
        Request(const char *method, const char *uri) {
            this->Init(method, uri);
            this->set_content("");
        }
        Request(const char *method, const char *uri, const char *content) {
            this->Init(method, uri);
            this->set_content(content);
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

            this->Init(method, uri);
            this->set_content(content.c_str());
        }
        ~Request() { }
        virtual bool write_content(nanosocket::Socket & sock) {
            if (sock.send(content_.c_str(), content_.size()) == (int)content_.size()) {
                return true;
            } else {
                return false;
            }
        }
        virtual void finalize_header() { }
        inline void set_header(const char* key, const char *val) {
            this->headers_.set_header(key, val);
        }
        inline void set_header(const char* key, size_t val) {
            this->headers_.set_header(key, val);
        }
        inline void push_header(const char* key, const char *val) {
            this->headers_.push_header(key, val);
        }
        bool write_header(nanosocket::Socket &sock, bool is_proxy) {
            // finalize content-length header
            this->finalize_header();

            this->set_header("Content-Length", content_length_);

            // make request string
            std::string hbuf =
                  method_ + " " + (is_proxy ? uri_.as_string() : uri_.path_query()) + " HTTP/1.0\r\n"
                + headers_.as_string()
                + "\r\n"
            ;

            // send it
            return sock.send(hbuf.c_str(), hbuf.size()) == (int)hbuf.size();
        }

        inline Headers *headers() { return &headers_; }
        inline Uri *uri() { return &uri_; }
        inline void set_uri(const char *uri) { uri_.parse(uri); }
        inline void set_uri(const std::string &uri) { this->set_uri(uri.c_str()); }
        inline std::string method() { return method_; }

    protected:
        inline void set_content(const char *content) {
            content_ = content;
            content_length_ = content_.size();
        }
        inline void Init(const char *method, const char *uri) {
            method_  = method;
            assert(uri_.parse(uri));
            this->set_header("User-Agent", NANOWWW_USER_AGENT);
            this->set_header("Host", uri_.host().c_str());
        }
    };

    /**
     * multipart/form-data request class.
     * see also RFC 1867.
     */
    class RequestFormData : public Request {
    private:
        enum PartType {
            PART_STRING,
            PART_FILE
        };
        class PartElement {
        public:
            PartElement(PartType type, const std::string &name, const std::string &value) {
                type_  = type;
                name_  = name;
                value_ = value;

                if (type == PART_STRING) {
                    size_ = value_.size();
                } else {
                    // get file length
                    size_ = 0;
                    FILE * fp = fopen(value_.c_str(), "r");
                    if (!fp) {return;}
                    if (fseek(fp, 0L, SEEK_END) != 0) { return; }
                    long len = ftell(fp);
                    if (len == -1) { return; }
                    if (fclose(fp) != 0) { return; }
                    size_ = len;
                }
            }
            inline std::string name() { return name_; }
            inline std::string value() { return value_; }
            inline std::string header() { return header_; }
            inline void push_header(std::string &header) { header_ = header; }
            inline PartType type() { return type_; }
            inline size_t size() { return size_; }
            bool send(nanosocket::Socket &sock, char *buf, size_t buflen) {
                if (type_ == PART_STRING) {
                    std::string buf;
                    buf += this->header();
                    buf += this->value();
                    buf += "\r\n";
                    if (sock.send(buf.c_str(), buf.size()) != (int)buf.size()) {
                        return false;
                    }
                    return true;
                } else {
                    if (sock.send(this->header().c_str(), this->header().size()) != (int)this->header().size()) {
                        return false;
                    }
                    FILE *fp = fopen(value_.c_str(), "rb");
                    if (!fp) {
                        return false;
                    }
                    while (!feof(fp)) {
                        size_t r = fread(buf, sizeof(char), buflen, fp);
                        if (r == 0) {
                            break;
                        }
                        while (r > 0) {
                            int sent = sock.send(buf, r);
                            if (sent < 0) {
                                return false;
                            }
                            r -= sent;
                        }
                    }
                    fclose(fp);
                    if (sock.send("\r\n", 2) != 2) {
                        return false;
                    }
                    return true;
                }
            }
        private:
            PartType type_;
            std::string name_;
            std::string value_;
            std::string header_;
            size_t size_;
        };
        std::vector<PartElement> elements_;
        std::string boundary_;
        size_t multipart_buffer_size_;
        char *multipart_buffer_;
    public:
        RequestFormData(const char *method, const char *uri):Request(method, uri) {
            this->Init(method, uri);
            boundary_ = RequestFormData::generate_boundary(10); // enough randomness

            std::string content_type("multipart/form-data; boundary=\"");
            content_type += boundary_;
            content_type += "\"";
            this->set_header("Content-Type", content_type.c_str());

            content_length_ = 0;

            multipart_buffer_size_ = NANOWWW_DEFAULT_MULTIPART_BUFFER_SIZE;
            multipart_buffer_ = new char [multipart_buffer_size_];
            assert(multipart_buffer_);
        }
        ~RequestFormData() {
            delete [] multipart_buffer_;
        }
        void set_multipart_buffer_size(size_t s) {
            multipart_buffer_size_ = s;
            delete [] multipart_buffer_;
            multipart_buffer_ = new char [s];
            assert(multipart_buffer_);
        }
        bool write_content(nanosocket::Socket & sock) {
            // send each elements
            std::vector<PartElement>::iterator iter = elements_.begin();
            for (;iter != elements_.end(); ++iter) {
                if (!iter->send(sock, multipart_buffer_, multipart_buffer_size_)) {
                    return false;
                }
            }

            // send terminater
            std::string buf;
            buf += std::string("--")+boundary_+"--\r\n";
            if (sock.send(buf.c_str(), buf.size()) != (int)buf.size()) {
                return false;
            }
            return true;
        }
        void finalize_header() {
            std::vector<PartElement>::iterator iter = elements_.begin();
            for (;iter != elements_.end(); ++iter) {
                std::string buf;
                buf += std::string("--")+boundary_+"\r\n";
                buf += std::string("Content-Disposition: form-data; name=\"")+iter->name()+"\"";
                if (iter->type() == PART_FILE) {
                    buf += std::string("; filename=\"");
                    buf += iter->value()  + "\"";
                }
                buf += "\r\n\r\n";
                iter->push_header(buf);
                content_length_ += buf.size();
                content_length_ += iter->size();
                content_length_ += 2;
            }
            content_length_ += sizeof("--")-1+boundary_.size()+sizeof("--\r\n")-1;
        }
        static inline std::string generate_boundary(int n) {
            srand(time(NULL));

            std::string sbuf;
            for (int i=0; i<n*3; i++) {
                sbuf += (float(rand())/RAND_MAX*256);
            }
            int bbufsiz = nb_base64_needed_encoded_length(sbuf.size());
            unsigned char * bbuf = new unsigned char[bbufsiz];
            assert(bbuf);
            nb_base64_encode((const unsigned char*)sbuf.c_str(), sbuf.size(), (unsigned char*)bbuf);
            std::string ret((char*)bbuf);
            delete [] bbuf;
            return ret;
        }
        inline std::string boundary() { return boundary_; }
        inline bool add_string(const std::string &name, const std::string &body) {
            elements_.push_back(PartElement(PART_STRING, name, body));
            return true;
        }
        inline bool add_file(const std::string &name, const std::string &fname) {
            elements_.push_back(PartElement(PART_FILE, name, fname));
            return true;
        }
    };

    class Client {
    private:
        std::string errstr_;
        unsigned int timeout_;
        int max_redirects_;
        Uri proxy_url_;
    public:
        Client() {
            timeout_ = 60; // default timeout is 60sec
            max_redirects_ = 7; // default. same as LWP::UA
        }
        /**
         * @args tiemout: timeout in sec.
         * @return none
         */
        inline void set_timeout(unsigned int timeout) {
            timeout_ = timeout;
        }
        inline unsigned int timeout() { return timeout_; }

        /// set proxy url
        inline bool set_proxy(std::string &proxy_url) {
            return proxy_url_.parse(proxy_url);
        }
        /// get proxy url
        inline std::string proxy() {
            return proxy_url_.as_string();
        }
        inline bool is_proxy() {
            return proxy_url_;
        }
        /**
         * @return string of latest error
         */
        inline std::string errstr() { return errstr_; }
        inline int send_get(Response *res, const std::string &uri) {
            return this->send_get(res, uri.c_str());
        }
        inline int send_get(Response *res, const char *uri) {
            Request req("GET", uri, "");
            return this->send_request(req, res);
        }
        inline int send_post(Response *res, const char *uri, std::map<std::string, std::string> &data) {
            Request req("POST", uri, data);
            return this->send_request(req, res);
        }
        inline int send_post(Response *res, const char *uri, const char *content) {
            Request req("POST", uri, content);
            return this->send_request(req, res);
        }
        inline int send_put(Response *res, const char *uri, const char *content) {
            Request req("PUT", uri, content);
            return this->send_request(req, res);
        }
        inline int send_delete(Response *res, const char *uri) {
            Request req("DELETE", uri, "");
            return this->send_request(req, res);
        }
        /**
         * @return return true if success
         */
        inline bool send_request(Request &req, Response *res) {
            return send_request_internal(req, res, this->max_redirects_);
        }
    protected:
        bool send_request_internal(Request &req, Response *res, int remain_redirect) {
            nanoalarm::Alarm alrm(this->timeout_); // RAII

            std::auto_ptr<nanosocket::Socket> sock;
            if (req.uri()->scheme() == "http") {
                nanosocket::Socket *p = new nanosocket::Socket();
                sock.reset(p);
            } else {
#ifdef HAVE_SSL
                nanosocket::Socket *p = new nanosocket::SSLSocket();
                sock.reset(p);
#else
                errstr_ = "your binary donesn't supports SSL";
                return false;
#endif
            }

            if (!proxy_url_) {
                short port =    req.uri()->port() == 0
                            ? (req.uri()->scheme() == "https" ? 443 : 80)
                            : req.uri()->port();
                if (!sock->connect(req.uri()->host().c_str(), port)) {
                    errstr_ = sock->errstr();
                    return false;
                }
            } else { // use proxy
                if (!sock->connect(proxy_url_.host().c_str(), proxy_url_.port())) {
                    errstr_ = sock->errstr();
                    return false;
                }
            }

            int opt = 1;
            sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int));

            if (!req.write_header(*sock, this->is_proxy())) {
                errstr_ = "error in writing header";
                return false;
            }
            if (!req.write_content(*sock)) {
                errstr_ = "error in writing body";
                return false;
            }

            // reading loop
            std::string buf;
            char read_buf[NANOWWW_READ_BUFFER_SIZE];

            // read header part
            while (1) {
                int nread = sock->recv(read_buf, sizeof(read_buf));
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
                        res->push_header(
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
                int nread = sock->recv(read_buf, sizeof(read_buf));
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

            sock->close();
            return true;
        }
        inline int max_redirects() { return max_redirects_; }
        inline void set_max_redirects(int mr) { max_redirects_ = mr; }
    };
};

#endif  // NANOWWW_H_
