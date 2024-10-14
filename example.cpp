#include "WinHTTP.hpp"
#include <iostream>

int main() {
    auto res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .GetRequest().Target(L"/").Send().Receive();
    
    res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .PostRequest().Target(L"api/forgotpassword")
    .AddFormData("email", {"abdussametersoylu@gmail.com"})
    .Send().Receive();

    std::cout << res << std::endl;

    return 0; 
}