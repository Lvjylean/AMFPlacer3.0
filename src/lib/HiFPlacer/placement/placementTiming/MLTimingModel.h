#ifndef _MLTIMINGMODEL
#define _MLTIMINGMODEL

/**
 * @brief ML-based timing delay model using FullMLP architecture.
 *        基于 FullMLP 架构的 ML 时序延迟模型。
 *
 * Replaces the polynomial model (Eq.4) with a trained neural network
 * for more accurate delay estimation. Supports runtime inference
 * without external dependencies.
 * 用训练好的神经网络替代多项式模型 (Eq.4), 提供更准确的延迟估计。
 * 运行时推理不需要外部依赖。
 *
 * Architecture: FullMLP with type embeddings
 *   Input: 13 tabular features -> type embeddings -> MLP -> delay
 *   Layers: 27 -> 256 -> 128 -> 64 -> 1
 *   Features: delta_x/y, delta_x_scaled, manhattan, euclidean,
 *             fanout, log_fanout, src/sink_type, cross_cr_x/y, cr_dist_x/y
 */

#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cassert>

class MLTimingModel
{
  public:
    // Cell type encoding (must match Python CELL_TYPE_MAP)
    // cell 类型编码 (必须与 Python CELL_TYPE_MAP 一致)
    static constexpr int N_CELL_TYPES = 22;
    static constexpr int EMBED_DIM = 8;
    static constexpr int N_FEATURES = 13;
    // Input dim: (N_FEATURES - 2 type cols) + 2 * EMBED_DIM = 11 + 16 = 27
    static constexpr int INPUT_DIM = N_FEATURES - 2 + EMBED_DIM * 2;

    MLTimingModel() : loaded_(false), residualMode_(false) {}

    /**
     * @brief Load model weights from JSON file.
     *        从 JSON 文件加载模型权重。
     * @param path Path to fullmlp_weights.json / 权重 JSON 文件路径
     * @return true if loaded successfully / 加载成功返回 true
     */
    bool loadFromJSON(const std::string &path);

    /**
     * @brief Predict delay between two points.
     *        预测两点之间的延迟。
     * @param X1, Y1 Source position / 源位置
     * @param X2, Y2 Destination position / 目标位置
     * @param fanout Net fanout / 网络 fanout
     * @param srcType Source cell type string / 源 cell 类型字符串
     * @param sinkType Sink cell type string / 目标 cell 类型字符串
     * @param clockRegionX0, clockRegionY0 Source clock region / 源时钟区域
     * @param clockRegionX1, clockRegionY1 Dest clock region / 目标时钟区域
     * @return Estimated delay in ns / 预估延迟 (ns)
     */
    float predict(float X1, float Y1, float X2, float Y2,
                  int fanout, int srcType, int sinkType,
                  int clockRegionX0, int clockRegionY0,
                  int clockRegionX1, int clockRegionY1) const;

    bool isLoaded() const { return loaded_; }
    bool isLUTMode() const { return lutLoaded_; }

    /**
     * @brief Load LUT (lookup table) distilled from ML model. Zero inference cost.
     *        加载从 ML 模型蒸馏的查找表。推理成本为零。
     * @param path Path to LUT JSON file / 查找表 JSON 文件路径
     * @return true if loaded successfully / 加载成功返回 true
     */
    bool loadLUT(const std::string &path);

    /**
     * @brief Predict delay using LUT interpolation (near-zero cost).
     *        使用 LUT 插值预测延迟 (接近零成本)。
     */
    float predictLUT(float X1, float Y1, float X2, float Y2,
                     int fanout, int srcType, int sinkType,
                     int clockRegionX0, int clockRegionY0,
                     int clockRegionX1, int clockRegionY1) const;

    /**
     * @brief Check if model is in residual mode (predicts actual - poly).
     *        检查模型是否为残差模式 (预测 实际值 - 多项式值)。
     */
    bool isResidualMode() const { return residualMode_; }

    /**
     * @brief Map C++ DesignCellType enum to Python CELL_TYPE_MAP encoding.
     *        将 C++ DesignCellType 枚举映射到 Python CELL_TYPE_MAP 编码。
     *
     * C++ enum order differs from Python training encoding.
     * Unknown types map to N_CELL_TYPES (22) = unknown embedding.
     * C++ 枚举顺序与 Python 训练编码不同，未知类型映射到 N_CELL_TYPES (22)。
     */
    static int cellTypeToMLType(int designCellType)
    {
        // C++ DesignCellType -> Python CELL_TYPE_MAP
        // C++ 枚举 -> Python 训练编码
        static const int mapping[] = {
            0,  // CellType_LUT1 = 0
            1,  // CellType_LUT2 = 1
            2,  // CellType_LUT3 = 2
            3,  // CellType_LUT4 = 3
            4,  // CellType_LUT5 = 4
            5,  // CellType_LUT6 = 5
            6,  // CellType_LUT6_2 = 6
            9,  // CellType_FDCE = 7
            10, // CellType_FDPE = 8
            7,  // CellType_FDRE = 9
            8,  // CellType_FDSE = 10
            22, // CellType_LDCE = 11 (unknown)
            22, // CellType_AND2B1L = 12 (unknown)
            11, // CellType_CARRY8 = 13
            22, // CellType_DSP48E2 = 14 (unknown)
            12, // CellType_MUXF7 = 15
            13, // CellType_MUXF8 = 16
            15, // CellType_SRL16E = 17
            16, // CellType_SRLC32E = 18
        };
        if (designCellType >= 0 && designCellType < 19)
            return mapping[designCellType];
        return N_CELL_TYPES; // unknown / 未知类型
    }

  private:
    bool loaded_;
    bool residualMode_; // Residual mode: output = actual - poly / 残差模式: 输出 = 实际 - 多项式

    // Type embeddings: [N_CELL_TYPES+1][EMBED_DIM]
    // 类型嵌入: [N_CELL_TYPES+1][EMBED_DIM]
    std::vector<std::vector<float>> srcEmbedWeight_;
    std::vector<std::vector<float>> sinkEmbedWeight_;

    // Network layers: weight[out_dim][in_dim], bias[out_dim]
    // 网络层: weight[out_dim][in_dim], bias[out_dim]
    struct Layer
    {
        std::vector<std::vector<float>> weight;
        std::vector<float> bias;
        bool hasRelu;
    };
    std::vector<Layer> layers_;

    // Feature normalization / 特征归一化
    std::vector<float> normMean_;
    std::vector<float> normStd_;
    std::vector<bool> normMask_; // which columns to normalize / 哪些列需要归一化

    // Forward pass helper / 前向传播辅助
    std::vector<float> forward(const std::vector<float> &input) const;

    // Clock region size constants (VCU108)
    // 时钟区域尺寸常量 (VCU108)
    static constexpr float CR_X_SIZE = 170.0f;
    static constexpr float CR_Y_SIZE = 60.0f;

    // ---- LUT v2 (lookup table) data / 查找表 v2 数据 ----
    // 5D: (distance, fanout, srcType, sinkType, crossCR) with bilinear interpolation
    // 5D: (距离, 扇出, 源类型, 目标类型, 跨时钟区域) 带双线性插值
    bool lutLoaded_ = false;
    std::vector<float> lutData_;       // flattened 5D array / 展平的 5D 数组
    std::vector<float> lutDistBins_;   // distance bin edges / 距离 bin 边界
    std::vector<float> lutFanBins_;    // log2(fanout) bin edges / log2(fanout) bin 边界
    int lutNDist_ = 0, lutNFan_ = 0, lutNSrc_ = 0, lutNSink_ = 0, lutNCr_ = 0;

    // LUT 5D indexing: [dist][fan][src][sink][cr]
    inline float lutAt5(int di, int fi, int si, int ki, int ci) const
    {
        int idx = (((di * lutNFan_ + fi) * lutNSrc_ + si) * lutNSink_ + ki) * lutNCr_ + ci;
        return lutData_[idx];
    }

    // Find bin index and interpolation weight / 找 bin 索引和插值权重
    static inline void findBinLerp(float val, const std::vector<float> &bins,
                                   int &lo, float &frac)
    {
        int n = (int)bins.size() - 1;
        for (int i = 0; i < n; i++)
        {
            if (val < bins[i + 1])
            {
                lo = i;
                float range = bins[i + 1] - bins[i];
                frac = (range > 1e-6f) ? (val - bins[i]) / range : 0.0f;
                return;
            }
        }
        lo = n - 1;
        frac = 1.0f;
    }
};

// ============================================================
// Inline implementation / 内联实现
// ============================================================

inline float MLTimingModel::predict(float X1, float Y1, float X2, float Y2,
                                    int fanout, int srcType, int sinkType,
                                    int clockRegionX0, int clockRegionY0,
                                    int clockRegionX1, int clockRegionY1) const
{
    if (!loaded_)
        return 0.05f;

    // Build 13 raw features / 构建 13 个原始特征
    float delta_x = std::fabs(X1 - X2);
    float delta_y = std::fabs(Y1 - Y2);
    float delta_x_scaled = delta_x * 2.0f;
    float manhattan = delta_x + delta_y;
    float euclidean = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    float fanout_f = static_cast<float>(fanout);
    float log_fanout = std::log1p(fanout_f);
    float src_type_f = static_cast<float>(std::min(srcType, N_CELL_TYPES));
    float sink_type_f = static_cast<float>(std::min(sinkType, N_CELL_TYPES));
    float cross_cr_x = (clockRegionX0 != clockRegionX1) ? 1.0f : 0.0f;
    float cross_cr_y = (clockRegionY0 != clockRegionY1) ? 1.0f : 0.0f;
    float cr_dist_x = static_cast<float>(std::abs(clockRegionX0 - clockRegionX1));
    float cr_dist_y = static_cast<float>(std::abs(clockRegionY0 - clockRegionY1));

    // Raw feature vector (13 dims) / 原始特征向量 (13 维)
    std::vector<float> raw = {
        delta_x, delta_y, delta_x_scaled, manhattan, euclidean,
        fanout_f, log_fanout, src_type_f, sink_type_f,
        cross_cr_x, cross_cr_y, cr_dist_x, cr_dist_y};

    // Normalize (skip cols 7,8 = type encodings) / 归一化 (跳过第 7,8 列)
    int normIdx = 0;
    for (int i = 0; i < N_FEATURES; i++)
    {
        if (normMask_[i])
        {
            raw[i] = (raw[i] - normMean_[normIdx]) / normStd_[normIdx];
            normIdx++;
        }
    }

    // Build MLP input: [x[0:7], x[9:13], src_emb, sink_emb] = 27 dims
    // 构建 MLP 输入: [x[0:7], x[9:13], src_emb, sink_emb] = 27 维
    std::vector<float> input(INPUT_DIM);
    int idx = 0;
    // x[0:7] = delta_x, delta_y, delta_x_scaled, manhattan, euclidean, fanout, log_fanout
    for (int i = 0; i < 7; i++)
        input[idx++] = raw[i];
    // x[9:13] = cross_cr_x, cross_cr_y, cr_dist_x, cr_dist_y
    for (int i = 9; i < 13; i++)
        input[idx++] = raw[i];

    // Type embeddings / 类型嵌入
    int srcIdx = std::max(0, std::min(srcType, N_CELL_TYPES));
    int sinkIdx = std::max(0, std::min(sinkType, N_CELL_TYPES));
    for (int i = 0; i < EMBED_DIM; i++)
        input[idx++] = srcEmbedWeight_[srcIdx][i];
    for (int i = 0; i < EMBED_DIM; i++)
        input[idx++] = sinkEmbedWeight_[sinkIdx][i];

    assert(idx == INPUT_DIM);

    // Forward pass / 前向传播
    std::vector<float> output = forward(input);

    if (residualMode_)
    {
        // Residual mode: return raw output (can be negative)
        // 残差模式: 返回原始输出 (可为负值)
        return output[0];
    }

    // Absolute mode: add base delay (matching Python model's +0.05 in forward pass)
    // 绝对模式: 加上基础延迟 (与 Python 模型 forward 中的 +0.05 一致)
    float delay = output[0] + 0.05f;
    if (delay < 0.05f)
        delay = 0.05f;
    return delay;
}

inline std::vector<float> MLTimingModel::forward(const std::vector<float> &input) const
{
    std::vector<float> current = input;

    for (const auto &layer : layers_)
    {
        int outDim = layer.bias.size();
        int inDim = layer.weight[0].size();
        std::vector<float> next(outDim);

        for (int i = 0; i < outDim; i++)
        {
            float sum = layer.bias[i];
            for (int j = 0; j < inDim; j++)
                sum += layer.weight[i][j] * current[j];

            // ReLU activation / ReLU 激活
            if (layer.hasRelu && sum < 0.0f)
                sum = 0.0f;

            next[i] = sum;
        }
        current = std::move(next);
    }
    return current;
}

// ============================================================
// LUT loading and prediction / 查找表加载和预测
// ============================================================

inline bool MLTimingModel::loadLUT(const std::string &path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        std::cerr << "MLTimingModel: cannot open LUT file: " << path << std::endl;
        return false;
    }

    // Read entire file / 读取整个文件
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    // Helper: extract JSON array of floats / 辅助: 提取 JSON 浮点数组
    auto extractFloatArray = [&](const std::string &key) -> std::vector<float> {
        std::vector<float> result;
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return result;
        pos = content.find("[", pos);
        size_t end = content.find("]", pos);
        std::string arr = content.substr(pos + 1, end - pos - 1);
        size_t p = 0;
        while (p < arr.size()) {
            while (p < arr.size() && (arr[p] == ' ' || arr[p] == ',' || arr[p] == '\n')) p++;
            if (p >= arr.size()) break;
            size_t start = p;
            while (p < arr.size() && arr[p] != ',' && arr[p] != ' ' && arr[p] != '\n' && arr[p] != ']') p++;
            result.push_back(std::stof(arr.substr(start, p - start)));
        }
        return result;
    };

    lutDistBins_ = extractFloatArray("dist_bins");
    lutFanBins_ = extractFloatArray("fanout_bins");

    auto shape = extractFloatArray("shape");
    // Support both v1 (6D) and v2 (5D) / 同时支持 v1 (6D) 和 v2 (5D)
    if (shape.size() == 5)
    {
        // v2: (dist, fan, src, sink, cr)
        lutNDist_ = (int)shape[0]; lutNFan_ = (int)shape[1];
        lutNSrc_ = (int)shape[2]; lutNSink_ = (int)shape[3]; lutNCr_ = (int)shape[4];
    }
    else
    {
        std::cerr << "MLTimingModel: LUT shape error (expected 5D, got " << shape.size() << "D)" << std::endl;
        return false;
    }

    lutData_ = extractFloatArray("data");
    int expected = lutNDist_ * lutNFan_ * lutNSrc_ * lutNSink_ * lutNCr_;
    if ((int)lutData_.size() != expected)
    {
        std::cerr << "MLTimingModel: LUT data size mismatch: " << lutData_.size()
                  << " vs " << expected << std::endl;
        return false;
    }

    lutLoaded_ = true;
    std::cout << "MLTimingModel: LUT v2 loaded, shape=("
              << lutNDist_ << "," << lutNFan_ << "," << lutNSrc_ << ","
              << lutNSink_ << "," << lutNCr_ << ") "
              << lutData_.size() << " entries" << std::endl;
    return true;
}

inline float MLTimingModel::predictLUT(float X1, float Y1, float X2, float Y2,
                                       int fanout, int srcType, int sinkType,
                                       int clockRegionX0, int clockRegionY0,
                                       int clockRegionX1, int clockRegionY1) const
{
    if (!lutLoaded_) return 0.1f;

    float dx = std::fabs(X1 - X2);
    float dy = std::fabs(Y1 - Y2);
    float manhattan = dx + dy;
    if (manhattan < 0.01f) manhattan = 0.01f;

    float logFanout = std::log2(std::max(1.0f, (float)fanout + 1.0f));
    int crCross = (clockRegionX0 != clockRegionX1 || clockRegionY0 != clockRegionY1) ? 1 : 0;

    // Clamp type indices / 限制类型索引范围
    int si = std::max(0, std::min(srcType, lutNSrc_ - 1));
    int ki = std::max(0, std::min(sinkType, lutNSink_ - 1));

    // Bilinear interpolation in (distance, fanout) / 在 (距离, 扇出) 维度双线性插值
    int dLo, fLo;
    float dFrac, fFrac;
    findBinLerp(manhattan, lutDistBins_, dLo, dFrac);
    findBinLerp(logFanout, lutFanBins_, fLo, fFrac);

    int dHi = std::min(dLo + 1, lutNDist_ - 1);
    int fHi = std::min(fLo + 1, lutNFan_ - 1);
    dLo = std::max(0, std::min(dLo, lutNDist_ - 1));
    fLo = std::max(0, std::min(fLo, lutNFan_ - 1));

    // 4-point bilinear interpolation / 4 点双线性插值
    float v00 = lutAt5(dLo, fLo, si, ki, crCross);
    float v10 = lutAt5(dHi, fLo, si, ki, crCross);
    float v01 = lutAt5(dLo, fHi, si, ki, crCross);
    float v11 = lutAt5(dHi, fHi, si, ki, crCross);

    float v0 = v00 + (v10 - v00) * dFrac;
    float v1 = v01 + (v11 - v01) * dFrac;
    float delay = v0 + (v1 - v0) * fFrac;

    return std::max(delay, 0.05f);
}

#endif
