#pragma once
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <winhttp.h>
#include <minwinbase.h>
#include <functional>
#include <optional>
#include <thread>
#include <winnt.h>
#include <shlobj.h>
#include <format>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace WinHTTP::Util {
    inline std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
        auto s = str;
        std::vector<std::string> tokens;
        size_t pos = 0;
        std::string token;
        while ((pos = s.find(delimiter)) != std::string::npos) {
            token = s.substr(0, pos);
            tokens.push_back(token);
            s.erase(0, pos + delimiter.length());
        }
        tokens.push_back(s);

        return tokens;
    }
}

namespace WinHTTP {
    class WinHTTP {
        public: 
        #pragma region TYPES
        enum class ProxyType {
            DefaultProxy = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            NoProxy = WINHTTP_ACCESS_TYPE_NO_PROXY,
            NamedProxy = WINHTTP_ACCESS_TYPE_NAMED_PROXY,
            AutomaticProxy = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        };
        enum class Error {
            None = 0,

            SessionCreationFailed,
            SessionNotAvailable,

            ConnectionFailed,
            ConnectionNotAvailable,

            RequestFailed,
            RequestNotAvailable,

            HeaderAddFailed,
        };
        class wstring_vector : public std::vector<std::wstring> {
        public:
            std::vector<LPCWSTR> to_lpcwstr() const {
                std::vector<LPCWSTR> ret;
                ret.reserve(this->size()); // Reserve space to avoid multiple allocations
                for (const auto& data : *this) {
                    ret.push_back(data.c_str());
                }
                return ret;
            }
        };
        enum class FormContentType {
            Text,
            File,
            AttachedFile
        };
        class FormContent { 
            public: 
            std::string data; 
            const FormContentType type = FormContentType::Text; 
            const std::string additionalData = "text";
        };
        class FormData {
            public: 
            std::string name;
            FormContent content; 
        };
        #pragma endregion

        #pragma region CLASS_CONSTRUCTORS
        explicit WinHTTP(const std::wstring& userAgent, ProxyType AccessType = ProxyType::DefaultProxy, const std::wstring& proxyName = L"", const std::wstring& proxyBypass = L"", DWORD flags = 0):
        requestSent(false), allowMultiThread(false), error(Error::None), ownerThreadId(std::this_thread::get_id()) {
            hSession = WinHttpOpen(userAgent.c_str(), 
                                to_underlying(AccessType), 
                                proxyName.empty() ? WINHTTP_NO_PROXY_NAME : proxyName.c_str(), 
                            proxyBypass.empty() ? WINHTTP_NO_PROXY_BYPASS : proxyName.c_str(), 
                                    flags);
            if(not hSession)
                SetError(Error::SessionCreationFailed);
        }

        WinHTTP(WinHTTP& other)  = delete;
        WinHTTP(WinHTTP&& other) = delete; 

        ~WinHTTP() {
            if(hRequest)    WinHttpCloseHandle(hRequest);
            if(hConnect)    WinHttpCloseHandle(hConnect);
            if(hSession)    WinHttpCloseHandle(hSession);
        }
        #pragma endregion

        // Connects to the given server.
        void Connect(const std::wstring& serverName, INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT, DWORD Reserved = WINHTTP_DEFAULT_ACCEPT_TYPES) {
            check_thread();
            if_session_available<void>([&] {
                hConnect = WinHttpConnect(hSession, serverName.c_str(), port, WINHTTP_DEFAULT_ACCEPT_TYPES);
                if(not hConnect) {
                SetError(Error::ConnectionFailed);
                return;
                }
                error = Error::None; 
            });
        }

        // Opens a request to the server. 
        // When object is destroyed, request is also closed. No need to close it manually. 
        void OpenRequest(const std::wstring& verb, const std::wstring& objectName, const std::wstring& version = L"", const std::wstring& referrer = L"", wstring_vector accept_types = {}, DWORD flags = 0) {
            check_thread();
            if_connection_available<void>([&] {
                hRequest = WinHttpOpenRequest(hConnect, verb.c_str(), objectName.c_str(), version.c_str(), 
                referrer.empty() ? NULL : referrer.c_str(), accept_types.empty() ? NULL : accept_types.to_lpcwstr().data(), flags);
            });
        }
        // Sends a request to the server.
        // Need an open request first. 
        bool SendRequest(const std::wstring& additional_headers = L"", DWORD headersLength = 0, LPVOID optional = WINHTTP_NO_REQUEST_DATA, DWORD optionalLength = 0, DWORD total_length = 0, DWORD_PTR context = NULL) {
            check_thread();
            if_request_available<void>([&] {
                requestSent = WinHttpSendRequest(hRequest, additional_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : additional_headers.c_str(), headersLength, optional, optionalLength, total_length, context);
            }, [&] {
                SetError(Error::RequestNotAvailable);
            });
            return requestSent; 
        }
        // Sends a multipart form data to the server.
        // Needs an open request first.
        bool SendMultiPartFormRequest(std::vector<FormData> form_data, const std::wstring& additional_headers = L"", DWORD headersLength = 0) {
            return if_request_available<bool>([&]() -> bool {
                std::string boundary = "----Boundary" + std::to_string((rand() % 999999) + 100000);
                std::stringstream formBody; 
                for(auto data : form_data) {
                    formBody << "--" + boundary + "\r\n";
                    formBody << std::format("Content-Disposition: form-data; name=\"{}\"", data.name);
                    if(data.content.type == FormContentType::Text) {   
                        formBody << "\r\n\r\n" << data.content.data << "\r\n"; 
                        continue;
                    } 
                    if(data.content.type == FormContentType::File) {
                        std::string base_filename = data.content.data.substr(data.content.data.find_last_of("/\\") + 1);
                        formBody << "; filename=\"" + base_filename + "\"\r\n";
                        std::string file_data = read_file_content(data.content.data);
                        formBody << std::format("Content-Type: {}\r\n\r\n", data.content.additionalData);
                        formBody << file_data + "\r\n"; 
                    }
                    if(data.content.type ==FormContentType::AttachedFile) {
                        auto adData = Util::split(data.content.additionalData, "|");
                        formBody << "; filename=\"" + adData[1] + "\"\r\n";
                        formBody << std::format("Content-Type: {}\r\n\r\n", adData[0]);
                        formBody << data.content.data + "\r\n";  
                    }
                }
                formBody << "--" + boundary + "--\r\n";
                std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end()) + L"\r\n";
                if (not WinHttpAddRequestHeaders(hRequest, headers.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD)) {
                    SetError(Error::HeaderAddFailed);
                    return false; 
                }

                auto requestdata = formBody.str();

                if (not WinHttpSendRequest(hRequest, additional_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : additional_headers.c_str(), headersLength,  (LPVOID)requestdata.data(), (DWORD)requestdata.size(), (DWORD)requestdata.size(), 0)) {
                    SetError(Error::RequestFailed);
                    return false; 
                }

                return true; 
            }, []{ return false; });
        }

        static std::pair<DWORD, std::string> GetLastErrorMessage() {
            DWORD errorCode = GetLastError();
            LPSTR errorBuffer = nullptr;

            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&errorBuffer,
                0,
                nullptr
            );

            std::string errorMessage = errorBuffer ? errorBuffer : "Unknown error";
            LocalFree(errorBuffer);
            return {errorCode, errorMessage.substr(0, errorMessage.length() - 2)};
        }
        // Receives a response from server. Needs an open request
        // and a request must be sent already.
        std::optional<std::string> ReceiveResponse(LPVOID reserved = NULL) {
        check_thread();
        return if_request_available<std::optional<std::string>>([&]() -> std::optional<std::string> {
            if (!WinHttpReceiveResponse(hRequest, reserved)) {
                return {};
            }

            std::stringstream ret; 
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;

            std::vector<char> buffer; // Use vector to manage memory automatically
            do {
                dwSize = 0;
                if (WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                    buffer.resize(dwSize); // Resize to required size
                    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                        ret.write(buffer.data(), dwDownloaded); // Write to stringstream
                    }
                } 
            } while (dwSize > 0);

            return ret.str();
        });
    }

        bool SessionAvailable() {
            return hSession; 
        }
        bool ConnectionAvailable() {
            return hConnect;
        }
        bool RequestAvailable() {
            return hRequest; 
        }
        void SetError(Error e) {
            check_thread();
            error = e; 
        }
        bool ErrorSet() {
            return error != Error::None;
        }
        Error GetError() const {
            return error;
        }
        std::string GetError(bool) {
            switch(error) {
                case Error::ConnectionFailed:
                    return "Connection failed!";
                case Error::ConnectionNotAvailable:
                    return "Connection not available!";
                case Error::RequestNotAvailable: 
                    return "Request not available!";
                case Error::RequestFailed: 
                    return "Request failed!";
                case Error::SessionCreationFailed: 
                    return "Session creation failed!"; 
                case Error::SessionNotAvailable: 
                    return "Session not available!";
                case Error::HeaderAddFailed:
                    return "Headers add failed!"; 
                case Error::None:
                    return "None";
            }
            return "";
        }
        bool MultiThreadAllowed() {
            return allowMultiThread;
        }
        void AllowMultiThread() {
            check_thread();
            allowMultiThread = true; 
        }
        void DisallowMultiThread() {
            allowMultiThread = false; 
        }
        private: 
        template<typename Ret_>
        Ret_ if_session_available(const std::function<Ret_()>& _if, const std::function<Ret_()>& _else = {}) {
            if(not SessionAvailable()) {
                SetError(Error::SessionNotAvailable);
                if(_else)
                    return _else();
                return Ret_{}; 
            }
            return _if();
        }
        template<typename Ret_>
        Ret_ if_connection_available(const std::function<Ret_()>& _if, const std::function<Ret_()>& _else = {}) {
            return if_session_available<Ret_>([&]() -> Ret_ {
                if(not ConnectionAvailable()) {
                    SetError(Error::ConnectionNotAvailable);
                    if(_else)
                        return _else();
                    return Ret_{}; 
                }
                return _if();
            });
        }
        template<typename Ret_>
        Ret_ if_request_available(const std::function<Ret_()>& _if, const std::function<Ret_()>& _else = {}) {
            return if_connection_available<Ret_>([&]() -> Ret_ {
                if(not RequestAvailable()) {
                    SetError(Error::RequestNotAvailable);
                    if(_else)
                        return _else();
                    return Ret_{};
                }
                return _if();
            }); 
        }
        template<>
        void if_session_available<void>(const std::function<void()>& _if, const std::function<void()>& _else) {
            if(not SessionAvailable()) {
                SetError(Error::SessionNotAvailable);
                if(_else)
                    _else();
                return; 
            }
            _if();
        }
        template<>
        void if_connection_available<void>(const std::function<void()>& _if, const std::function<void()>& _else) {
            if_session_available<void>([&]() -> void {
                if(not ConnectionAvailable()) {
                    SetError(Error::ConnectionNotAvailable);
                    if(_else)
                        _else();
                    return; 
                }
                _if();
            });
        }
        template<>
        void if_request_available<void>(const std::function<void()>& _if, const std::function<void()>& _else) {
            if_connection_available<void>([&]() -> void {
                if(not RequestAvailable()) {
                    SetError(Error::RequestNotAvailable);
                    if(_else)
                        _else();
                    return;
                }
                _if();
            }); 
        }
        void check_thread() const {
            if(allowMultiThread)
                return; 
            if (std::this_thread::get_id() != ownerThreadId) {
                throw std::runtime_error("Attempt to use WinHTTP class from a different thread. If you know what you are doing, you can suppress this error with AllowMultiThread() method.");
            }
        }

        std::string read_file_content(const std::string& filePath) {
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file) {
                throw std::runtime_error("Failed to open file.");
            }

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::string buffer(size, '\0');
            if (!file.read(buffer.data(), size)) {
                throw std::runtime_error("Failed to read file.");
            }

            return buffer;
        }
        template <typename T_>
        constexpr std::underlying_type_t<T_> to_underlying(T_ obj) noexcept {
            return static_cast<std::underlying_type_t<T_>>(obj);
        }
        HINTERNET hSession, hConnect, hRequest;
        bool requestSent, allowMultiThread; 
        Error error;
        std::thread::id ownerThreadId;
    };
    
    class HTTPBuilder {
        private:
        WinHTTP session;
        class Response {
            public:
            Response(HTTPBuilder * owner) : owner(owner) {}
            std::string Receive() {
                auto resp = owner->session.ReceiveResponse();
                if(not resp) {
                    throw std::runtime_error("Recieve failed!");
                }
                return *resp; 
            }
            private:
            HTTPBuilder * owner;
        };

        template<typename ReqType>
        class SetTarget {
            public:
            SetTarget(HTTPBuilder * owner) : owner(owner) {} 
            ReqType Target(const std::wstring& target) {
                return {owner, target};
            }
            private: 
            HTTPBuilder * owner;
        };

        template<typename ReqType>
        class Request {
            public: 
            Request() : owner(nullptr) {}
            Request(HTTPBuilder * owner, const std::wstring& verb) : owner(owner), verb(verb) {}
            Request(HTTPBuilder * owner, const std::wstring& verb, const std::wstring& target) : owner(owner), verb(verb), objectName(target) {}
            ReqType& Version(const std::wstring& version) {
                this->version = version;
                return *static_cast<ReqType*>(this);
            }

            ReqType& Referrer(const std::wstring& referrer) {
                this->referrer = referrer; 
                return *static_cast<ReqType*>(this);
            }

            ReqType& AcceptTypes(WinHTTP::wstring_vector accept_types) {
                this->accept_types = accept_types;
                return *static_cast<ReqType*>(this);
            }

            ReqType& Flags(DWORD flags) {
                this->flags = flags;
                return *static_cast<ReqType*>(this);
            }
            
            protected:
            HTTPBuilder* owner;
            const std::wstring verb = L"";
            std::wstring objectName = L"", version = L"", referrer = L"";
            WinHTTP::wstring_vector accept_types = {};
            DWORD flags = 0;
        };

        class PostRequest : public Request<PostRequest> {
            public:
            PostRequest(HTTPBuilder *owner) : Request(owner, L"POST") {}
            PostRequest(HTTPBuilder *owner, const std::wstring& target) : Request(owner, L"POST", target) {}

            PostRequest& AddFormData(const std::string& key, const WinHTTP::FormContent& content) {
                formData.emplace_back(key, content);
                return *this;
            }
            Response& Send() {
                if(not owner->session.ConnectionAvailable())
                    throw std::runtime_error("Connection not available! Error code: " + std::to_string(owner->session.GetLastErrorMessage().first));
                if(formData.empty()) 
                    throw std::runtime_error("Form data must be set to send!");
                owner->session.OpenRequest(verb, objectName, version, referrer, accept_types, flags);
                if(not owner->session.RequestAvailable()) 
                    throw std::runtime_error("An error occured while opening request! Error code: " + std::to_string(owner->session.GetLastErrorMessage().first));
                owner->session.SendMultiPartFormRequest(formData);
                return *new Response{owner};
            }
            private:
            std::vector<WinHTTP::FormData> formData;
        };

        class GetRequest : public Request<GetRequest> {
            public:
            GetRequest(HTTPBuilder *owner) : Request(owner, L"GET") {}
            GetRequest(HTTPBuilder *owner, const std::wstring& target) : Request(owner, L"POST", target) {}
            //Sends the request and returns a response
            Response& Send() {
                if(not owner->session.ConnectionAvailable())
                    throw std::runtime_error("Connection not available! Error code: " + std::to_string(owner->session.GetLastErrorMessage().first));
                if(objectName.empty()) {
                    throw std::runtime_error("Target must be set!");
                }
                owner->session.OpenRequest(verb, objectName, version, referrer, accept_types, flags);
                if(not owner->session.RequestAvailable()) 
                    throw std::runtime_error("An error occured while opening request! Error code: " + std::to_string(owner->session.GetLastErrorMessage().first));
                owner->session.SendRequest();

                return *new Response{owner};
            }
            private:
        };



        class Connection {
            public:
            Connection() : owner(nullptr) {}
            Connection(HTTPBuilder* owner) : owner(owner) {}
            SetTarget<GetRequest> GetRequest() {
                return SetTarget<class GetRequest>{owner};
            }
            SetTarget<PostRequest> PostRequest() {
                return SetTarget<class PostRequest>{owner};
            }
            private:
            HTTPBuilder* owner;
        };

        public:
        HTTPBuilder(const std::wstring& userAgent) : session(userAgent) {}
        Connection& Connect(const std::wstring& serverName, INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT, DWORD Reserved = WINHTTP_DEFAULT_ACCEPT_TYPES) {
            session.Connect(serverName, port, Reserved);
            return *new Connection{this};
        }
    };
}




