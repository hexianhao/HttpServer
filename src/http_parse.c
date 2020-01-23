#include <gperftools/tcmalloc.h>

#include "http.h"
#include "http_parse.h"


int http_parse_request_line(http_request_t *request) {
    unsigned char ch, *p, *m;
    size_t i;

    enum {
        start = 0,
        method,
        spaces_before_uri,
        after_slash_in_uri,
        http,
        http_H,
        http_HT,
        http_HTT,
        http_HTTP,
        first_major_digit,
        major_digit,
        first_minor_digit,
        minor_digit,
        spaces_after_digit,
        almost_done
    } state;

    state = request->state;
    
    for (i = request->pos; i < request->last; i++)
    {
        p = (unsigned char *)&request->buf[i % MAX_BUF];
        ch = *p;

        switch (state)
        {

        /* HTTP methods: GET, HEAD, POST */
        case start:
            request->request_start = p;
            
            if(ch == CR || ch == LF) {
                break;
            }

            if((ch < 'A' || ch > 'Z') && ch != '_') {
                return HTTP_PARSE_INVALID_METHOD;
            }

            state = method;
            break;
        
        case method:
            if(ch == ' ') {
                request->method_end = p;
                m = request->request_start;

                switch(p - m) {

                case 3:
                    if(str3cmp(m, 'G', 'E', 'T', ' ')) { 
                        request->method = HTTP_GET;
                        break;
                    }
                    break;

                case 4:
                    if(str3cmp(m, 'P', 'O', 'S', 'T')) {
                        request->method = HTTP_POST;
                        break;
                    }

                    if(str3cmp(m, 'H', 'E', 'A', 'D')) {
                        request->method = HTTP_HEAD;
                        break;
                    }

                    break;

                default:
                    request->method = HTTP_UNKNOWN;
                    break;
                }
                state = spaces_before_uri;
                break;
            }

            if ((ch < 'A' || ch > 'Z') && ch != '_') {
                return HTTP_PARSE_INVALID_METHOD;
            }
            break;
        
        /* space* before URI */
        case spaces_before_uri:
            if(ch == '/') {
                request->uri_start = p;
                state = after_slash_in_uri;
                break;
            }

            switch(ch) {
                case ' ':
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
        case after_slash_in_uri:
            switch(ch) {
                case ' ':
                    request->uri_end = p;
                    state = http;
                    break;
                default:
                    break;
            }
            break;
        
        /* space+ after URI */
        case http:
            switch (ch) {
                case ' ':
                    break;
                case 'H':
                    state = http_H;
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
        case http_H:
            switch (ch) {
                case 'T':
                    state = http_HT;
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
         case http_HT:
            switch (ch) {
                case 'T':
                    state = http_HTT;
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
        case http_HTT:
            switch (ch) {
                case 'P':
                    state = http_HTTP;
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
        case http_HTTP:
            switch (ch) {
                case '/':
                    state = first_major_digit;
                    break;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        
        /* first digit of major HTTP version */
        case first_major_digit:
            if(ch < '1' || ch > '9') {
                return HTTP_PARSE_INVALID_REQUEST;
            }

            request->http_major = ch - '0';
            state = major_digit;
            break;
        
        /* major HTTP version or dot */
        case major_digit:
            if (ch == '.') {
                state = first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return HTTP_PARSE_INVALID_REQUEST;
            }

            request->http_major = request->http_major * 10 + ch - '0';
            break;
        
        /* first digit of minor HTTP version */
        case first_minor_digit:
            if (ch < '0' || ch > '9') {
                return HTTP_PARSE_INVALID_REQUEST;
            }

            request->http_minor = ch - '0';
            state = minor_digit;
            break;
        
        /* minor HTTP version or end of request line */
        case minor_digit:
            if (ch == CR) {
                state = almost_done;
                break;
            }

            if (ch == LF) {
                goto done;
            }

            if (ch == ' ') {
                state = spaces_after_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return HTTP_PARSE_INVALID_REQUEST;
            }

            request->http_minor = request->http_minor * 10 + ch - '0';
            break;
        
        case spaces_after_digit:
            switch (ch) {
                case ' ':
                    break;
                case CR:
                    state = almost_done;
                    break;
                case LF:
                    goto done;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        /* end of request line */
        case almost_done:
            request->request_end = p - 1;
            switch (ch) {
                case LF:
                    goto done;
                default:
                    return HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        }
    }

    request->pos = i;
    request->state = state;

    return AGAIN;

done:
    request->pos = i + 1;

    if(request->request_end == NULL) {
        request->request_end = p;
    }

    request->state = start;

    return RETURN_OK;
} 


int http_parse_request_body(http_request_t *request) {
    unsigned char ch, *p;
    size_t i;

    enum {
        start = 0,
        key,
        spaces_before_colon,
        spaces_after_colon,
        value,
        cr,
        crlf,
        crlfcr
    } state;

    state = request->state;
    if(state != 0) {
        return -1;
    }

    http_header_t *hd;
    for(i = request->pos; i < request->last; i++) {
        p = (unsigned char *)&request->buf[i % MAX_BUF];
        ch = *p;

        switch (state)
        {
        case start:
            if(ch == CR || ch == LF) {
                break;
            }

            request->cur_header_key_start = p;
            state = key;
            break;
        
        case key:
            if(ch == ' ') {
                request->cur_header_key_end = p;
                state = spaces_before_colon;
                break;
            }

            if(ch == ':') {
                request->cur_header_key_end = p;
                state = spaces_after_colon;
                break;
            }
            break;
        
        case spaces_before_colon:
            if(ch == ' ') {
                break;
            } else if(ch == ':') {
                state = spaces_after_colon;
                break;
            } else {
                return HTTP_PARSE_INVALID_HEADER;
            }
        
        case spaces_after_colon:
            if(ch == ' ') {
                break;
            }

            state = value;
            request->cur_header_value_start = p;
            break;
        
        case value:
            if(ch == CR) {
                request->cur_header_value_end = p;
                state = cr;
            }

            if(ch == LF) {
                request->cur_header_key_end = p;
                state = crlf;
            }

            break;
        
        case cr:
            if(ch == LF) {
                state = crlf;
                // save the current http header
                hd = (http_header_t *)tc_malloc(sizeof(http_header_t));
                hd->key_start = request->cur_header_key_start;
                hd->key_end = request->cur_header_key_end;
                hd->value_start = request->cur_header_value_start;
                hd->value_end = request->cur_header_value_end;

                list_add(&(hd->list), &(request->list));
                
                break;
            } else {
                return HTTP_PARSE_INVALID_HEADER;
            }
        
        case crlf:
            if(ch == CR) {
                state = crlfcr;
            } else {
                request->cur_header_key_start = p;
                state = key;
            }
            break;
        
        case crlfcr:
            switch(ch) {
                case LF:
                    goto done;

                default:
                    return HTTP_PARSE_INVALID_HEADER;
            }
            break;

        }
    }
    request->pos = i;
    request->state = state;

    return AGAIN;

done:
    request->pos = i + 1;
    request->state = start;

    return RETURN_OK;
}