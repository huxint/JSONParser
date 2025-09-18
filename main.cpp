#include <variant>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <iostream>
#include <charconv>
#include <optional>
#include <cctype>
#include <regex>
#include <ranges>
#include <fstream>

class JSON;
using JSONList = std::vector<JSON>; // 因为是指针
using JSONDict = std::unordered_map<std::string, JSON>;// 啊，我不会实现键值也是JSON的，先不支持了

class JSON {
public:
    using Type = std::variant<std::nullptr_t, bool, std::int32_t, double, std::string, JSONList, JSONDict>;
public:
    JSON(Type inner = std::nullptr_t{}) : _inner(inner) {}

    template <class T>
    bool is() const {
        return std::holds_alternative<T>(_inner);
    }

    Type const &get() const {
        return _inner;
    }

    Type &get() {
        return _inner;
    }

    template <typename T>
    T const &get() const {
        return std::get<T>(_inner);
    }

    template <typename T>
    T &get() {
        return std::get<T>(_inner);
    }

    friend std::ostream &operator <<(std::ostream &ostream, const JSON &self) {
        self.pretty_print(ostream, 0);
        return ostream;
    }

private:
    Type _inner;
    // JSON格式化输出函数
    void pretty_print(std::ostream &ostream, std::size_t indent = 0) const {
        std::visit([&](auto const &value) {
            if constexpr (std::same_as<JSONList, std::decay_t<decltype(value)>>) {
                if (value.empty()) {
                    ostream << "[]";
                } else {
                    ostream << "[\n";
                    for (size_t i = 0; i < value.size(); ++i) {
                        ostream << std::string(2 * (indent + 2), ' ');
                        value[i].pretty_print(ostream, indent + 2);
                        if (i + 1 != value.size()) {
                            ostream << ",";
                        }
                        ostream << "\n";
                    }
                    ostream << std::string(2 * indent, ' ') << "]";
                }
            } else if constexpr (std::same_as<JSONDict, std::decay_t<decltype(value)>>) {
                if (value.empty()) {
                    ostream << "{}";
                } else {
                    ostream << "{\n";
                    size_t i = 0;
                    for (auto const &[key, val] : value) {
                        ostream << std::string(2 * (indent + 2), ' ') << "\"" << key << "\": ";
                        val.pretty_print(ostream, indent + 2);
                        if (i != value.size() - 1) {
                            ostream << ",";
                        }
                        ostream << "\n";
                        ++i;
                    }
                    ostream << std::string(2 * indent, ' ') << "}";
                }
            } else if constexpr (std::same_as<std::string, std::decay_t<decltype(value)>>) {
                ostream << "\"" << value << "\"";
            } else if constexpr (std::same_as<bool, std::decay_t<decltype(value)>>) {
                ostream << (value ? "true" : "false");
            } else {
                ostream << value;
            }
        }, _inner);
    }
};

constexpr auto skip_ws = " \n\r\t\v\f\0";

std::string read(std::string const &path) {
    std::ifstream inf(path);
    return std::move(std::string(std::istreambuf_iterator<char>(inf), std::istreambuf_iterator<char>()));
}

constexpr char escape_char(char c) {
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        case '0': return '\0';
        default: return c;
    }
    return c;
}

template <typename T>
std::optional<T> parse_number(std::string_view str) {
    T value;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec == std::errc{} && ptr == str.data() + str.size()) {
        return value;
    }
    return std::nullopt;
}

std::pair<std::string, size_t> parse_string(std::string_view json, char quote) {
    std::string str;
    enum {
        NORMAL,
        ESCAPE,
    } state = NORMAL;
    std::size_t i;
    for (i = 1; i < json.size(); ++i) {
        auto c = json[i];
        if (state == NORMAL) {
            if (c == '\\') {
                state = ESCAPE;
            } else if (c == quote) {
                ++i;
                break;
            } else {
                str += c;
            }
        } else if (state == ESCAPE) {
            str += escape_char(c);
            state = NORMAL;
        }
    }
    return {std::move(str), i};
}

std::pair<JSON, std::size_t> parse(std::string_view json) {
    if (json.empty()) {
        return {JSON{std::nullptr_t{}}, 0};
    } else if (std::size_t offset = json.find_first_not_of(skip_ws); offset != 0 && offset != std::string_view::npos) {// 找到不是这些字符开头的位置   != 0 是为了不死循环
        auto [obj, eaten] = parse(json.substr(offset));
        return {std::move(obj), eaten + offset};
    } else if (auto first = std::tolower(json[0]); first == 't' || first == 'f' || first == 'n') {
        std::regex re{"(true|false|null)", std::regex_constants::icase};// 忽略大小写
        std::cmatch match;
        if (std::regex_search(json.data(), json.data() + json.size(), match, re)) {
            auto str = match.str();
            if (auto c = std::tolower(str[0]); c == 't') {
                return {JSON{true}, str.size()};
            } else if (c == 'f') {
                return {JSON{false}, str.size()};
            } else if (c == 'n') {
                return {JSON{std::nullptr_t{}}, str.size()};
            }
        }
    } else if (std::isdigit(json[0]) || json[0] == '+' || json[0] == '-') {
        std::regex re(R"([+-]?[0-9]+(\.[0-9]*)?([eE][+-]?[0-9]+)?)");// 整数 浮点数 科学计数法
        std::cmatch match;
        if (std::regex_search(json.data(), json.data() + json.size(), match, re)) {
            if (auto str = match.str(); auto number = parse_number<int>(str)) {
                return {JSON{*number}, str.size()};
            } else if (auto number = parse_number<double>(str)) {
                return {JSON{*number}, str.size()};
            }
        }
    } else if (json[0] == '"' || json[0] == '\'') {// 支持单引号的字符串
        auto [str, eaten] = parse_string(json, json[0]);
        return {JSON{std::move(str)}, eaten};
    } else if (json[0] == '[') {
        std::vector<JSON> res;
        std::size_t i;
        for (i = 1; i < json.size();) {
            if (json[i] == ']') {
                ++i;
                break;
            }
            auto [obj, eaten] = parse(json.substr(i));
            if (eaten == 0) {
                i = 0;
                break;
            }
            i += eaten;
            res.push_back(std::move(obj));
            i += json[i] == ',';

            if (std::size_t offset = json.find_first_not_of(skip_ws, i); offset != std::string_view::npos) {
                i = offset;
            }
            // 这里要跳过一些字符
        }
        return {JSON{std::move(res)}, i};
    } else if (json[0] == '{') {
        JSONDict res;
        std::size_t i;
        for (i = 1; i < json.size();) {
            if (json[i] == '}') {
                ++i;
                break;
            }
            auto [key, key_eaten] = parse(json.substr(i));
            if (key_eaten == 0) {
                i = 0;
                break;
            }
            i += key_eaten;

            if (!key.is<std::string>()) {// 我们要求键值是string
                i = 0;
                break;
            }

            if (std::size_t offset = json.find_first_not_of(skip_ws, i); offset != std::string_view::npos) {
                i = offset;
            }
            // 键值后面要跳过一些字符

            i += json[i] == ':';

            auto [obj, obj_eaten] = parse(json.substr(i));

            // 这里不需要跳过， 因为parse里面会跳过
            if (obj_eaten == 0) {
                i = 0;
                break;
            }
            i += obj_eaten;

            res.insert_or_assign(std::move(key.get<std::string>()), std::move(obj));// 后来者居上, 自己实现的map需要有insert_or_assign

            if (std::size_t offset = json.find_first_not_of(skip_ws, i); offset != std::string_view::npos) {
                i = offset;
            }
            // 逗号前面要跳过一些字符
            i += json[i] == ',';
            // 逗号后面要跳过一些字符， 但是这里不需要写，因为下一个循环会进入parse自己跳过
        }
        return {JSON{std::move(res)}, i};
    }
    return {JSON{std::nullptr_t{}}, 0};
}

int main() {
    auto str = read("../test.json");
    freopen("../out.json", "w", stdout);

    auto [json, eaten] = parse(str);
    std::cout << json << "\n";
    return 0;
}