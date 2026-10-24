#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace gc {

// ---- 素材类型（AssetSpec.type）----
enum class AssetType {
    Character, // 角色
    Item,      // 物品/道具
    Scene      // 场景概念图
};

inline QString assetTypeToString(AssetType t)
{
    switch (t) {
    case AssetType::Character: return QStringLiteral("character");
    case AssetType::Item:      return QStringLiteral("item");
    case AssetType::Scene:     return QStringLiteral("scene");
    }
    return QStringLiteral("item");
}

inline AssetType assetTypeFromString(const QString& s)
{
    if (s == QLatin1String("character")) return AssetType::Character;
    if (s == QLatin1String("scene"))     return AssetType::Scene;
    return AssetType::Item;
}

inline QString assetTypeToDisplay(AssetType t)
{
    switch (t) {
    case AssetType::Character: return QStringLiteral("角色");
    case AssetType::Item:      return QStringLiteral("物品");
    case AssetType::Scene:     return QStringLiteral("场景概念图");
    }
    return QStringLiteral("物品");
}

// ---- 方向（四方向 MVP）----
enum class Direction {
    Down,  // 正面（朝下/朝镜头）
    Up,    // 背面
    Left,
    Right
};

inline QString directionToString(Direction d)
{
    switch (d) {
    case Direction::Down:  return QStringLiteral("down");
    case Direction::Up:    return QStringLiteral("up");
    case Direction::Left:  return QStringLiteral("left");
    case Direction::Right: return QStringLiteral("right");
    }
    return QStringLiteral("down");
}

inline Direction directionFromString(const QString& s)
{
    if (s == QLatin1String("up"))    return Direction::Up;
    if (s == QLatin1String("left"))  return Direction::Left;
    if (s == QLatin1String("right")) return Direction::Right;
    return Direction::Down;
}

inline QString directionToDisplay(Direction d)
{
    switch (d) {
    case Direction::Down:  return QStringLiteral("下(正面)");
    case Direction::Up:    return QStringLiteral("上(背面)");
    case Direction::Left:  return QStringLiteral("左");
    case Direction::Right: return QStringLiteral("右");
    }
    return QStringLiteral("下");
}

// 生成稳定 ID：prefix-yyyyMMddHHmmss-xxxx
inline QString newId(const QString& prefix)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    QString shortUid = QUuid::createUuid().toString(QUuid::Id128).left(6);
    return prefix + QLatin1Char('-') + ts + QLatin1Char('-') + shortUid;
}

inline QString timeStampTag()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

} // namespace gc
