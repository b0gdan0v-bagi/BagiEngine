#pragma once

namespace Core {

    class String {
    public:
        static std::vector<std::string_view> Split(std::string_view str, char delimiter);
    };
    

}  // namespace Core

