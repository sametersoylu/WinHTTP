#include "WinHTTP.hpp"
#include <iostream>

int main() {
    auto res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .GetRequest().Target(L"/").Send().Recieve();
    
    res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .PostRequest().Target(L"/api/login")
    .AddFormData("email", {"mail@example.com"})
    .AddFormData("password", {"1234!.1234"})
    .Send().Recieve();

    std::cout << res << std::endl;

    return 0; 
}