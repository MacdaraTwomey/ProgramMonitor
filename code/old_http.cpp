
// TODO: Make all memory freed allocated where we want etc.
// TODO: async


constexpr int MAX_HTML_ELEMENT_LENGTH = 2000;


bool
read_recieved_data(HINTERNET request, u8 *buffer, size_t *byte_count)
{
    // read data must be big enough.
    Assert(buffer && byte_count);
    
    size_t total_recieved = 0;
    
    DWORD available_bytes = 0;
    do
    {
        BOOL query_success =  WinHttpQueryDataAvailable(request, &available_bytes);
        if (!query_success)
        {
            tprint("No query");
            break;
        }
        
        if (available_bytes == 0) break;
        
        // TODO: Read directly into read_data
        u8 *response_buffer = (u8 *)xalloc(available_bytes + 1);
        memset(response_buffer, 0, available_bytes + 1);
        
        // If you are using WinHttpReadData synchronously, and the return value is TRUE and the number of bytes read is zero, the transfer has been completed and there are no more bytes to read on the handle.
        DWORD bytes_read = 0;
        BOOL read_success = WinHttpReadData(request, response_buffer, available_bytes, &bytes_read);
        if (!read_success)
        {
            tprint("No read");
            free(response_buffer);
            break;
        }
        
        if (bytes_read == 0)
        {
            tprint("0 bytes read, not sure if this is an error because can try to read when nothing is there and are at EOF");
        }
        
        memcpy(buffer + total_recieved, response_buffer, bytes_read);
        total_recieved += bytes_read;
        
        free(response_buffer);
    } while(available_bytes > 0);
    
    tprint("Read % bytes", total_recieved);
    
    *byte_count = total_recieved;
    
    if (total_recieved == 0)
    {
        return false;
    }
    
    return true;
}


bool
make_get_request(HINTERNET connection, u8 **message , size_t *length, wchar_t *target, const wchar_t **types, bool is_SSL)
{
    // TODO: Handle non-SSL
    
    // target and types can be null
    
    const wchar_t **accepted_types = (types) ? types : WINHTTP_DEFAULT_ACCEPT_TYPES;
    DWORD flags = (is_SSL) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", target, NULL, WINHTTP_NO_REFERER, accepted_types, flags);
    if (request == NULL)
    {
        tprint("icon location request failed");
        return false;
    }
    
    defer(WinHttpCloseHandle(request));
    
    BOOL sent = WinHttpSendRequest(request,
                                   WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent)
    {
        tprint("No send");
        return false;
    }
    
    BOOL recieved = WinHttpReceiveResponse(request, NULL);
    if (!recieved)
    {
        tprint("No recieve");
        //print_error();
        return false;
    }
    
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
    if (dwStatusCode != HTTP_STATUS_OK)
    {
        tprint("Not OK, status code: %", dwStatusCode);
        return false;
    }
    
    //TODO : query how much space to alloc
    u8 *recieved_data = (u8 *)xalloc(Megabytes(8));
    size_t total_recieved = 0;
    bool read_success = read_recieved_data(request, recieved_data, &total_recieved);
    if (!read_success)
    {
        // free
        tprint("Could not read icon location data");
        return false;
    }
    
    *message = recieved_data;
    *length = total_recieved;
    
    return true;
}

String
search_html_for_icon_url(String html_page)
{
    // TODO: HTML can have spaces like rel = "value"
    // SCOPUS has rel="SHORTCUT ICON" ???
    
#define REL_ATTRIBUTE(value) "rel=\"" value "\""
    
    char *rel_attributes[] = {
        //REL_ATTRIBUTE("shortcut icon"),
        REL_ATTRIBUTE("icon"),
        REL_ATTRIBUTE("shortcut icon"),
    };
    
#undef REL_ATTRIBUTE
    
    String link_tag = {};
    for (int i = 0; i < array_count(rel_attributes); ++i)
    {
        i32 pos = search_for_substr(html_page, make_string(rel_attributes[i], strlen(rel_attributes[i])));
        if (pos != -1)
        {
            i32 tag_start = reverse_search_for_char(html_page, pos, '<');
            if (tag_start == -1) break;
            i32 tag_end = search_for_char(html_page, pos, '>');
            if (tag_end == -1) break;
            
            i32 length = tag_end - tag_start + 1;
            link_tag = substr_len(html_page, tag_start, length);
            break;
        }
    }
    
    String icon_url = {};
    if (link_tag.str)
    {
        i32 pos = search_for_substr(link_tag, make_string_from_literal("href"));
        if (pos != -1)
        {
            i32 start = search_for_char(link_tag, pos, '"');
            if (start != -1)
            {
                i32 end = search_for_char(link_tag, start + 1, '"');
                if (end != -1)
                {
                    i32 length = (end-1) - (start+1) + 1;
                    if (length > 0)
                    {
                        icon_url = substr_len(link_tag, start+1, length);
                    }
                }
            }
        }
    }
    
    return icon_url;
}

bool
request_icon_from_url(wchar_t *url, u8 **icon_data, size_t *size)
{
    // many WinHTTP error codes not supported by FormatMessage
    DWORD error = 0;
    auto print_error = [&](){ error = GetLastError(); tprint("Error code: %", error); };
    
    HINTERNET session = WinHttpOpen(L"WinHttpPostSample",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL)
    {
        tprint("No open");
        print_error();
        return false;
    }
    
    defer(WinHttpCloseHandle(session));
    
    // WinHttp does not accept international host names without converting them first to Punycode
    
    // Don't want https:// at start or a slash '/' at end both will give error
    HINTERNET connection = WinHttpConnect(session, url, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == NULL)
    {
        tprint("No connection");
        print_error();
        return false;
    }
    defer(WinHttpCloseHandle(connection));
    
    bool is_SSL = true;
    
    // This is all unicode from here to when we use wide chars
    u8 *message = nullptr;
    size_t message_len = 0;
    bool get_request_success = make_get_request(connection, &message, &message_len, nullptr, nullptr, is_SSL);
    if (!get_request_success)
    {
        tprint("Could make get request");
        return false;
    }
    
    // We null terminate the message ourselves
    message[message_len] = '\0';
    
    defer(free(message));
    
    String html_page = make_string(message, message_len);
    
    const wchar_t *default_types[] = {
        L"image/vnd.microsoft.icon",
        L"image/png",
        L"image/x-icon",
        nullptr,
    };
    
    wchar_t default_url_path[] = L"/favicon.ico";
    wchar_t *used_url_path = default_url_path;
    
    // These may be filled and used instad of defaults
    wchar_t url_path[4000];
    wchar_t new_connection_url_path[2000];
    bool open_new_connection = false;
    
    wchar_t temp_url[4000];
    
    // href can be:
    //   - An absolute URL - points to another web site (like href="http://www.example.com/theme.css")
    //   - A relative URL - points to a file within a web site (like href="/themes/theme.css")
    
    String icon_url = search_html_for_icon_url(html_page);
    if (icon_url.length > 0)
    {
        
        // TODO: Can __http__ absolute url just go www.website.com/images/icon.png?
        int temp_url_length = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, icon_url.str, icon_url.length,
                                                  temp_url, array_count(temp_url));
        if (temp_url_length > 0)
        {
            temp_url[temp_url_length] = 0;
            
            // WinHttpCrackUrl does not check the validity or format of a URL before attempting to crack it. As a result, if a string such as ""http://server?Bad=URL"" is passed in, the function returns incorrect results.
            const DWORD default_value = -1;
            
            URL_COMPONENTS components = {};
            components.dwStructSize = sizeof(components);
            // Set to non zero value to tell function to get these components, if the value remains the same
            // after the call, the components were not parsed.
            components.dwSchemeLength    = default_value;
            components.dwHostNameLength  = default_value;
            components.dwUrlPathLength   = default_value;
            
            //If the Internet protocol of the URL passed in for pwszUrl is not HTTP or HTTPS, then WinHttpCrackUrl returns FALSE
            BOOL result = WinHttpCrackUrl(temp_url, temp_url_length, 0, &components);
            if (result)
            {
                if (components.dwSchemeLength != default_value &&
                    components.dwSchemeLength > 0)
                {
                    //
                }
                if (components.dwHostNameLength != default_value &&
                    components.dwHostNameLength > 0)
                {
                    // relative url
                }
            }
            else
            {
                int e = GetLastError();
                int a = 3;
            }
        }
    }
    
    
    
    
    
    
#if 0
    
    if (prefix_match_case_insensitive(icon_url, make_string_from_literal("https")) ||
        prefix_match_case_insensitive(icon_url, make_string_from_literal("http")))
    {
        // New buffer because we might want to copy from the cracked url to url path
        wchar_t temp_url[4000];
        int absolute_url_length = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED,
                                                      icon_url.str, icon_url.length, // src
                                                      absolute_url, array_count(absolute_url)); // dest
        if (absolue_url_length > 0)
        {
            absolute_url[absolute_url_length] = 0;
            
            // WinHttpCrackUrl does not check the validity or format of a URL before attempting to crack it. As a result, if a string such as ""http://server?Bad=URL"" is passed in, the function returns incorrect results.
            const DWORD default_value = -1;
            
            URL_COMPONENTS components = {};
            components.dwStructSize = sizeof(components);
            // Set to non zero value to tell function to get these components, if the value remains the same
            // after the call, the components were not parsed.
            components.dwSchemeLength    = default_value;
            components.dwHostNameLength  = default_value;
            components.dwUrlPathLength   = default_value;
            
            BOOL result = WinHttpCrackUrl(absolute_url_length, absolute_url_length, 0, &components);
            if (result)
            {
                if (components.dwSchemeLength != default_value &&
                    components.dwSchemeLength != 0 &&
                    components.dwHostNameLength != default_value &&
                    components.dwHostNameLength != 0 &&
                    components.dwUrlPathLength != default_value &&
                    components.dwUrlPathLength != 0 &&
                    (components.nScheme == INTERNET_SCHEME_HTTP ||
                     components.nScheme == INTERNET_SCHEME_HTTPS))
                {
                    // Compare max of chars
                    if (wcsncmp(url, components.lpszHostName, components.dwHostNameLength) == 0)
                    {
                        // Have same host still
                        wcsncpy(url_path, components.lpszHostName, components.dwHostNameLength);
                        url_path[components.dwHostNameLength] = 0;
                        used_url_path = url_path;
                    }
                    else
                    {
                        // Open a connection with another host
                        open_new_connection = true;
                        
                        // Reuse this buffer
                        wcsncpy(url_path, components.lpszHostName, components.dwHostNameLength);
                        url_path[components.dwHostNameLength] = 0;
                        
                        wcsncpy(new_connection_url_path, components.lpszUrlPath, components.dwUrlPathLength);
                        new_connection_url_path[components.dwUrlPathLength] = 0;
                    }
                }
            }
        }
    }
    else
    {
        // Relative path or wrong protocol, or not prefixed with http:// but still is absolute path (e.g. file://)
        int url_path_length = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED,
                                                  icon_url.str, icon_url.length, // src
                                                  url_path, array_count(url_path)); // dest
        if (url_path_length > 0)
        {
            url_path[url_path_length] = 0;
            used_url_path = url_path;
        }
    }
}

#endif

if (open_new_connection)
{
    
}

u8 *icon_file = nullptr;
size_t file_size = 0;
bool icon_request_success = make_get_request(connection, &icon_file, &file_size, url_path, default_types, is_SSL);
if (!icon_request_success)
{
    tprint("Could make get request");
    return false;
}

// when in synchronous mode can recieve response when sendrequest returns
// When WinHttpReceiveResponse completes successfully, the status code and response headers have been received and are available for the application to inspect using WinHttpQueryHeaders. An application must call WinHttpReceiveResponse before it can use WinHttpQueryDataAvailable and WinHttpReadData to access the response entity body (if any).


//if (request) WinHttpCloseHandle(request);
//if (connection) WinHttpCloseHandle(connection);
//if (session) WinHttpCloseHandle(session);

*icon_data = icon_file;
*size = file_size;

return true;
}
