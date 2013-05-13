#pragma once

namespace hal
{

std::string& ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
}

std::string& rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
}

std::string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

std::vector<std::string>& split(const std::string& s, char delim, std::vector<std::string>& elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    return split(s, delim, elems);
}

template <class T>
void StrToVal( T& t, const std::string& sValue )
{
    std::istringstream iss( sValue );
    iss >> t;
}

template <class T>
T StrToVal( const std::string& sValue )
{
    T t;
    std::istringstream iss( sValue );
    iss >> t;
    return t;
}

template <class T>
std::string ValToStr( const T& t )
{
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

}