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
    using RequestFn       = std::function<GFDHttpResult(const std::string&)>;
    using LoginFinishedFn = std::function<void(bool, std::string)>;

    static bool        has_valid_session(const AppConfig* config);
    static std::string current_auth_token(const AppConfig* config);
    static void        clear_session(AppConfig* config);
    static void        logout(AppConfig* config, bool forget_credentials = false);
    static bool        ensure_logged_in(wxWindow* parent, std::string* error_message = nullptr);
    static bool        ensure_logged_in_async(wxWindow* parent, LoginFinishedFn finished);
    // Startup-only gate: validates the consumer session with the user-info API,
    // refreshes a still-valid token on the same cadence as the mobile app, and
    // falls back to the SMS login dialog when the service rejects the session.
    static bool ensure_startup_session(wxWindow* parent, std::string* error_message = nullptr);
    static bool perform_authenticated_request(const RequestFn& request,
                                              std::string&     body,
                                              std::string&     error_message,
                                              wxWindow*        parent = nullptr);
    static bool is_auth_failure_response(unsigned status, const std::string& body, const std::string& error_message);
    static bool is_retryable_auth_failure_response(unsigned status, const std::string& body, const std::string& error_message);
};

} // namespace GUI
} // namespace Slic3r

#endif
