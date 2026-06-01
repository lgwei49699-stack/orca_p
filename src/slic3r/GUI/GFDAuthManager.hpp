#pragma once

#ifndef slic3r_GFDAuthManager_hpp_
#define slic3r_GFDAuthManager_hpp_

#include <functional>
#include <string>

class wxWindow;

namespace Slic3r {

class AppConfig;

namespace GUI {

struct GFDHttpResult
{
    bool        ok{false};
    unsigned    status{0};
    std::string body;
    std::string error_message;
};

class GFDAuthManager
{
public:
    using RequestFn = std::function<GFDHttpResult(const std::string&)>;

    static bool        has_valid_session(const AppConfig* config);
    static std::string current_auth_token(const AppConfig* config);
    static void        clear_session(AppConfig* config);
    static bool        ensure_logged_in(wxWindow* parent, std::string* error_message = nullptr);
    static bool        perform_authenticated_request(const RequestFn& request,
                                                     std::string&     body,
                                                     std::string&     error_message,
                                                     wxWindow*        parent = nullptr);
    static bool        is_auth_failure_response(unsigned status, const std::string& body, const std::string& error_message);
};

}} // namespace Slic3r::GUI

#endif
