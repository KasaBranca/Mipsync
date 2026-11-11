#include "ResultRenderer.h"

#include <sstream>

namespace MipsyncEngine::Command {
namespace {

std::string Scalar(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return value.dump();
    return value.dump();
}

void RenderObject(std::ostringstream& out, const Json& object, int indent) {
    const std::string pad(static_cast<size_t>(indent), ' ');
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().is_object()) {
            out << pad << it.key() << ":\n";
            RenderObject(out, it.value(), indent + 2);
        } else if (it.value().is_array()) {
            out << pad << it.key() << ":";
            if (it.value().empty()) { out << " []\n"; continue; }
            out << "\n";
            for (const auto& item : it.value()) {
                if (item.is_object()) {
                    out << pad << "  - ";
                    bool first = true;
                    for (auto field = item.begin(); field != item.end(); ++field) {
                        if (!field.value().is_primitive()) continue;
                        if (!first) out << ", ";
                        out << field.key() << "=" << Scalar(field.value());
                        first = false;
                    }
                    out << "\n";
                } else {
                    out << pad << "  - " << Scalar(item) << "\n";
                }
            }
        } else {
            out << pad << it.key() << ": " << Scalar(it.value()) << "\n";
        }
    }
}

} // namespace

Json ProjectOutput(const Json& value, const std::string& fields, int maxDepth) {
    auto selectPath = [&](auto&& self, const Json& input, const std::string& path) -> Json {
        if (input.is_array()) {
            Json result=Json::array();
            for (const auto& item:input) result.push_back(self(self,item,path));
            return result;
        }
        const auto dot=path.find('.'); const auto key=path.substr(0,dot);
        if (!input.is_object() || !input.contains(key)) return nullptr;
        return {{key, dot==std::string::npos ? input.at(key) : self(self,input.at(key),path.substr(dot+1))}};
    };
    auto merge = [&](auto&& self, Json& to, const Json& from) -> void {
        if (to.is_array() && from.is_array() && to.size()==from.size()) {
            for(size_t i=0;i<to.size();++i)self(self,to[i],from[i]);
        } else if (to.is_object() && from.is_object()) {
            for(auto it=from.begin();it!=from.end();++it)self(self,to[it.key()],it.value());
        } else if (!from.is_null()) to=from;
    };
    Json result=value;
    if (!fields.empty()) {
        result=nullptr; std::istringstream paths(fields); std::string path;
        while(std::getline(paths,path,',')) {
            const auto first=path.find_first_not_of(" \t"),last=path.find_last_not_of(" \t");
            if(first!=std::string::npos)merge(merge,result,selectPath(selectPath,value,path.substr(first,last-first+1)));
        }
    }
    auto limit = [&](auto&& self, Json& item, int depth) -> void {
        if (!item.is_structured()) return;
        if (depth==0) { item={{"omitted",true},{"count",item.size()}}; return; }
        for(auto& child:item)self(self,child,depth-1);
    };
    if(maxDepth>=0)limit(limit,result,maxDepth);
    return result;
}

std::string RenderHuman(const std::string&, const CommandResult& result) {
    std::ostringstream out;
    if (!result.success) {
        for (const auto& diagnostic : result.diagnostics) {
            out << diagnostic.severity << "[" << diagnostic.code << "]: " << diagnostic.message;
            if (!diagnostic.location.is_null() && !diagnostic.location.empty()) {
                out << " (" << diagnostic.location.value("file", std::string{})
                    << ":" << diagnostic.location.value("line", 0)
                    << ":" << diagnostic.location.value("column", 0) << ")";
            }
            out << "\n";
        }
        return out.str();
    }
    if (result.value.is_object()) RenderObject(out, result.value, 0);
    else if (result.value.is_array()) {
        for (const auto& item : result.value)
            out << (item.is_primitive() ? Scalar(item) : item.dump(2)) << "\n";
    } else if (!result.value.is_null()) out << Scalar(result.value) << "\n";
    return out.str();
}

} // namespace MipsyncEngine::Command
