#pragma once

// AssetFactory：C++ 素材工厂（技术路线第 9 章）。
// AI 原始输出 → 游戏可用素材的确定性处理链：
//   背景去除 → 内容裁切 → 整数倍降采样(像素网格) → 调色板量化 → 脚底锚点对齐 → Sprite Sheet 打包
// 全部为纯函数，不产生副作用；原始图片永远由 ProjectStore 负责保存，工厂不覆盖任何文件。

#include "core/AssetSpec.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QVector>

namespace gc {

struct StandardizeResult {
    QImage frame;      // frameSize x frameSize 标准帧（透明画布，bottom-center 锚点）
    QImage preview;    // 4x 整数放大预览（最近邻，保持像素感）
    QVector<QRgb> palette;
    QRect contentRect; // 处理前内容包围盒
};

class AssetFactory {
public:
    // ---- 背景处理 ----
    // 从四边泛洪去除纯色背景（以四角/边缘主色为键色），tolerance 为 RGB 欧氏距离阈值
    static QImage removeBackground(const QImage& src, int tolerance = 40);

    // ---- 裁切与缩放 ----
    static QRect contentBounds(const QImage& src, int alphaThreshold = 8);
    static QImage cropToContent(const QImage& src);
    // 像素友好降采样：内部块取众数颜色（抗涂抹），边界块取最暗不透明色（轮廓保护）
    static QImage downscaleBlock(const QImage& src, int targetSize);
    // 最近邻整数放大（用于预览与导出放大版）
    static QImage upscaleNearest(const QImage& src, int factor);

    // ---- 调色板 ----
    static QVector<QRgb> buildPalette(const QImage& src, int maxColors); // 中位切分
    // 从原始输出构建「母版色板」：背景去除→裁切→降采样→中位切分
    static QVector<QRgb> paletteFromRaw(const QImage& raw, const AssetSpec& spec);
    static QImage quantize(const QImage& src, const QVector<QRgb>& palette,
                           bool dither = false);
    static int  colorCount(const QImage& src, int alphaThreshold = 8);

    // ---- 帧标准化 ----
    // 将内容放置到 frameSize x frameSize 透明画布，底边对齐、水平居中（脚底锚点）
    static QImage normalizeFrame(const QImage& content, int frameSize);

    // ---- 完整处理链 ----
    // sharedPalette 非空时使用统一色板（角色多方向防色彩漂移）；否则从本图提取
    static StandardizeResult standardize(const QImage& raw, const AssetSpec& spec,
                                         const QVector<QRgb>& sharedPalette = {});

    // ---- Sprite Sheet ----
    // rows: 每行一个方向的帧序列（MVP 每方向 1 帧）；返回图集与元数据 JSON
    static QImage packSheet(const QVector<QVector<QImage>>& rows, int frameSize);
    static QJsonObject sheetMetadata(const AssetSpec& spec,
                                     const QVector<QVector<Direction>>& rowDirs,
                                     int frameSize,
                                     const QVector<QRgb>& palette);
};

} // namespace gc
