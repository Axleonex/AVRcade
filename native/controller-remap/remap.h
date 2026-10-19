#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <vector>

namespace avrcade {
using Mapping = std::unordered_map<std::string, std::string>;
inline bool InputPath(const std::string& path) {
    const bool hand = path.rfind("/user/hand/left/input/",0)==0 || path.rfind("/user/hand/right/input/",0)==0;
    return hand && (path.ends_with("/click") || path.ends_with("/value") || path.ends_with("/force"));
}
inline std::string Remap(const Mapping& map, const std::string& path) {
    // Tracking, touch sensing, vibration and sticks' tilt are never reassigned.
    if (!InputPath(path)) return path;
    const auto it=map.find(path);
    return it==map.end()?path:it->second;
}
inline Mapping Parse(const std::string& data,const std::string& profile) {
    Mapping result; std::istringstream input(data); std::string line;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        const auto first=line.find('|'), second=line.find('|',first==std::string::npos?first:first+1);
        if(first==std::string::npos || second==std::string::npos || line.substr(0,first)!=profile) continue;
        auto from=line.substr(first+1,second-first-1),to=line.substr(second+1);
        if(!InputPath(from)||!InputPath(to)||from.substr(0,16)!=to.substr(0,16)) continue;
        result[from]=to;
    }
    return result;
}
}
