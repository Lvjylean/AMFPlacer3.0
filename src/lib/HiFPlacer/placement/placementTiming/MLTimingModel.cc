/**
 * @brief MLTimingModel weight loading implementation.
 *        MLTimingModel 权重加载实现。
 */

#include "MLTimingModel.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>

// Minimal JSON array parser for weight loading
// 最小化 JSON 数组解析器用于加载权重
namespace
{

// Skip whitespace / 跳过空白
void skipWS(const std::string &s, size_t &pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t'))
        pos++;
}

// Parse a float / 解析浮点数
float parseFloat(const std::string &s, size_t &pos)
{
    skipWS(s, pos);
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+'))
        pos++;
    while (pos < s.size() && (std::isdigit(s[pos]) || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' ||
                              s[pos] == '-' || s[pos] == '+'))
        pos++;
    std::string numStr = s.substr(start, pos - start);
    if (numStr.empty())
        throw std::runtime_error("Empty number at pos " + std::to_string(start) +
                                 ", context: '" + s.substr(start, std::min((size_t)30, s.size() - start)) + "'");
    // Use strtod to avoid std::stof throwing on subnormal floats
    // 使用 strtod 避免 std::stof 在次正规浮点数上抛异常
    char *end = nullptr;
    double val = std::strtod(numStr.c_str(), &end);
    if (end == numStr.c_str())
        throw std::runtime_error("stof failed on '" + numStr + "' at pos " + std::to_string(start));
    return static_cast<float>(val);
}

// Parse a 1D array of floats / 解析一维浮点数组
std::vector<float> parse1D(const std::string &s, size_t &pos)
{
    std::vector<float> result;
    skipWS(s, pos);
    if (s[pos] != '[')
        throw std::runtime_error("Expected '['");
    pos++;
    skipWS(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        result.push_back(parseFloat(s, pos));
        skipWS(s, pos);
        if (s[pos] == ',')
            pos++;
    }
    pos++; // skip ']'
    return result;
}

// Parse a 2D array of floats / 解析二维浮点数组
std::vector<std::vector<float>> parse2D(const std::string &s, size_t &pos)
{
    std::vector<std::vector<float>> result;
    skipWS(s, pos);
    if (s[pos] != '[')
        throw std::runtime_error("Expected '['");
    pos++;
    skipWS(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        result.push_back(parse1D(s, pos));
        skipWS(s, pos);
        if (s[pos] == ',')
            pos++;
        skipWS(s, pos);
    }
    pos++; // skip ']'
    return result;
}

// Parse a 1D array of bools (JSON true/false) / 解析布尔数组
std::vector<bool> parseBoolArray(const std::string &s, size_t &pos)
{
    std::vector<bool> result;
    skipWS(s, pos);
    if (s[pos] != '[')
        throw std::runtime_error("Expected '['");
    pos++;
    skipWS(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        if (s.substr(pos, 4) == "true")
        {
            result.push_back(true);
            pos += 4;
        }
        else if (s.substr(pos, 5) == "false")
        {
            result.push_back(false);
            pos += 5;
        }
        skipWS(s, pos);
        if (s[pos] == ',')
            pos++;
        skipWS(s, pos);
    }
    pos++; // skip ']'
    return result;
}

// Find key in JSON string / 在 JSON 字符串中查找键
size_t findKey(const std::string &s, const std::string &key, size_t startPos = 0)
{
    std::string pattern = "\"" + key + "\"";
    size_t pos = s.find(pattern, startPos);
    if (pos == std::string::npos)
        return std::string::npos;
    pos += pattern.size();
    // Skip to ':' / 跳到 ':'
    while (pos < s.size() && s[pos] != ':')
        pos++;
    pos++; // skip ':'
    skipWS(s, pos);
    return pos;
}

// Parse string value / 解析字符串值
std::string parseString(const std::string &s, size_t &pos)
{
    skipWS(s, pos);
    if (s[pos] != '"')
        throw std::runtime_error("Expected '\"'");
    pos++;
    size_t start = pos;
    while (pos < s.size() && s[pos] != '"')
        pos++;
    std::string result = s.substr(start, pos - start);
    pos++; // skip '"'
    return result;
}

// Find matching closing brace for '{' at pos, returns pos after '}'
// 查找 pos 处 '{' 对应的 '}', 返回 '}' 之后的位置
size_t findMatchingBrace(const std::string &s, size_t pos)
{
    int depth = 0;
    bool inString = false;
    while (pos < s.size())
    {
        char c = s[pos];
        if (inString)
        {
            if (c == '"')
                inString = false;
        }
        else
        {
            if (c == '"')
                inString = true;
            else if (c == '{')
                depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                    return pos + 1;
            }
        }
        pos++;
    }
    return s.size();
}

} // anonymous namespace

bool MLTimingModel::loadFromJSON(const std::string &path)
{
    // Read entire file / 读取整个文件
    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        std::cerr << "[MLTimingModel] ERROR: Cannot open " << path << std::endl;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    try
    {
        // Parse src_embed_weight / 解析 src 嵌入权重
        size_t pos = findKey(content, "src_embed_weight");
        srcEmbedWeight_ = parse2D(content, pos);

        // Parse sink_embed_weight / 解析 sink 嵌入权重
        pos = findKey(content, "sink_embed_weight");
        sinkEmbedWeight_ = parse2D(content, pos);

        // Parse layers / 解析网络层
        pos = findKey(content, "layers");
        // layers is an array of objects / layers 是对象数组
        skipWS(content, pos);
        if (content[pos] != '[')
            throw std::runtime_error("Expected '[' for layers");
        pos++;

        layers_.clear();
        while (pos < content.size())
        {
            skipWS(content, pos);
            if (content[pos] == ']')
                break;
            if (content[pos] == ',')
            {
                pos++;
                continue;
            }
            if (content[pos] == '{')
            {
                // Find matching '}' for this layer object
                // 查找此层对象的匹配 '}'
                size_t objEnd = findMatchingBrace(content, pos);
                std::string obj = content.substr(pos, objEnd - pos);

                Layer layer;

                size_t wPos = findKey(obj, "weight");
                layer.weight = parse2D(obj, wPos);

                size_t bPos = findKey(obj, "bias");
                layer.bias = parse1D(obj, bPos);

                size_t aPos = findKey(obj, "activation");
                std::string act = parseString(obj, aPos);
                layer.hasRelu = (act == "relu");

                layers_.push_back(std::move(layer));
                pos = objEnd;
            }
            else
            {
                pos++;
            }
        }

        // Parse normalization params / 解析归一化参数
        pos = findKey(content, "norm_mean");
        normMean_ = parse1D(content, pos);

        pos = findKey(content, "norm_std");
        normStd_ = parse1D(content, pos);

        pos = findKey(content, "norm_mask");
        normMask_ = parseBoolArray(content, pos);

        // Parse residual mode flag / 解析残差模式标志
        residualMode_ = false;
        size_t resPos = findKey(content, "residual");
        if (resPos != std::string::npos)
        {
            skipWS(content, resPos);
            residualMode_ = (content.substr(resPos, 4) == "true");
        }

        loaded_ = true;
        std::cout << "[MLTimingModel] Loaded from " << path
                  << " (" << layers_.size() << " layers, "
                  << srcEmbedWeight_.size() << " type embeddings"
                  << (residualMode_ ? ", RESIDUAL mode" : "") << ")" << std::endl;
        std::cout << "[MLTimingModel] 已加载 " << path
                  << (residualMode_ ? " (残差模式)" : " (绝对模式)") << std::endl;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[MLTimingModel] ERROR parsing " << path << ": " << e.what() << std::endl;
        loaded_ = false;
        return false;
    }
}
