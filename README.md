# WinHTTP 

## class WinHTTP 
This is a wrapper class for Windows's HTTP api "WinHTTP". One use only class. Initialize, use and destroy.
Not thread safe. Neither has to be. As again, one use only.

Usage is simple. First you start a session and open a connection to desired server. Note that connection will be started at the moment you called `Connect()`.
```cpp
using WinHTTP = WinHTTP::WinHTTP;
/* ... */
WinHTTP session(L"exampleUserAgent");
session.Connect(L"servername", 80);
/* ... */
```

Then we open a request to the server. We have to give request type (only get and post supported atm), and the request destination on the server. Then we can send a request.
```cpp
session.OpenRequest(L"GET", L"/");
session.SendRequest();
```
If we want to recieve the response, we can simply call `WinHTTP::RecieveResponse()`. It returns `std::optional<std::string>`. So it can return empty, be careful. 
```cpp
auto result = session.RecieveResponse()
if(result)
    std::cout << *result << std::endl;
```

For post requests, the starting is same and proccess is similar with a few key changes. First of all, only multi-part form requests are supported. The reason is they are suitable for simple post requests too. 

```cpp
session.OpenRequest(L"POST", L"api/login");
session.SendMultiPartFormRequest({
    { /* key: */ "email", /* content: */ {"mail@example.com"}},
    { "password", {"somesecurepassword"}}
});
```

The above code will send the form data. If you want to recieve the response, it's the same as the get request. 

You can also send complex data types but before that you MUST turn them into string in binary format, so they can be reconstructed by the server. 

```cpp
// let's say you already converted an image to binary format and it's called imageData
session.SendMultiPartFormRequest({
    {"image", /* content: */ {imageData, WinHTTP::FormContentType::AttachedFile, "image/png|image.png"}}
});
```
In content, we have 3 variables. First one is the data that is going to be sent. Second one is type of the data. We have only 3 available at the moment, `Text`, `File`, `AttachedFile`.

If you don't specify the type, by default it's `Text`. 

If you choose `File`, you need to give the file path to the first argument, so this class will go ahead and read the data of the file and will attach it, it self. The third part becomes available when you choose this option. It's going to be used for specifying the file type. It has to be HTTP header formatted. Be careful. (Not recommended.) 

If you choose `AttachedFile`, you give the data of the file, class won't read it, it expects the data. In the third part, as the `File`, you have to specify the HTTP header formatted file type and file name. The two are separated with `|` sign.  

## class HTTPBuilder
This is a helper class for WinHTTP class. It helps you to build requests in a more readable manner.

For get request;
```cpp
auto res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
.GetRequest().Target(L"/").Send().Recieve();
std::cout << res << std::endl;
```
Note that if this class can't get any response from server, it'll throw an exception unlike the wrapper. 

For post request;
```cpp
res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .PostRequest().Target(L"/api/login")
    .AddFormData("email", {"mail@example.com"})
    .AddFormData("password", {"somesecurepassword"})
    .Send().Recieve();
```
The builder has a specific order for you to build without making mistakes. So don't worry, you can't use things in wrong order. 

```cpp
res = WinHTTP::HTTPBuilder{L"example"}.Connect(L"localhost", 8000)
    .PostRequest().Target(L"/api/image/upload")
    .AddFormData("image", {imageData, WinHTTP::FormContentType::AttachedFile, "image/png|image.png"})
    .Send().Recieve();
```

To import your project, you only include WinHTTP.hpp, that's all. C++ 20 is required. 