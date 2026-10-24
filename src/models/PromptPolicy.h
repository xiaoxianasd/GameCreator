#pragma once

// PromptPolicy：结构化提示词策略（系统提示词层）。
// images 接口没有独立的 system 角色，因此由本模块把
//   固定生产规则 + StylePack 约束 + 用户描述 + 方向 VIEW + 一致性要求
// 组合成最终提示词；负面清单独立输出供 negative_prompt / {{NEGATIVE}} 使用。

#include "core/AssetSpec.h"

#include <QString>
#include <QStringList>

namespace gc {

class PromptPolicy {
public:
    // ---- 基础生产规则（角色母版 / 方向共用骨架）----
    static QString basePolicy(const AssetSpec& spec)
    {
        // 目标逻辑像素规格（角色常见 64x80；此处以 frameSize 生成近似方形骨架）
        return QStringLiteral(
            "You are a production pixel-art asset generator for an original 2.5D JRPG diorama.\n"
            "Visual requirements:\n"
            "- crisp hand-crafted pixel art\n"
            "- readable silhouette at native resolution\n"
            "- compact JRPG overworld proportions\n"
            "- one-logical-pixel dark navy-brown outer contour\n"
            "- selective inner outlines, not every region outlined\n"
            "- layered hair, clothing, accessories and equipment\n"
            "- three to four controlled value levels per material\n"
            "- clear highlights, midtones, core shadows and small accent pixels\n"
            "- restrained cohesive palette\n"
            "- %1 key light\n"
            "- %2 game perspective\n"
            "- feet aligned to the same bottom-center anchor\n"
            "- plain solid background, no environment, no floor, no text\n"
            "- original design, not an existing copyrighted character\n"
            "Pixel specification:\n"
            "- designed for a %3x%4 logical-pixel sprite\n"
            "- hard square pixel clusters\n"
            "- no anti-aliased edges, no smooth vector curves, no subpixel details\n"
            "- details must remain readable at 1x scale")
            .arg(spec.styleLight, spec.stylePerspective)
            .arg(spec.frameSize)
            .arg(spec.type == AssetType::Character ? int(spec.frameSize * 1.25) : spec.frameSize);
    }

    // ---- 方向 VIEW 约束 ----
    static QString viewDirective(Direction d)
    {
        switch (d) {
        case Direction::Down:
            return QStringLiteral(
                "VIEW: front-facing overworld sprite.\n"
                "Keep the approved identity and outfit unchanged.");
        case Direction::Up:
            return QStringLiteral(
                "VIEW: back-facing overworld sprite.\n"
                "Keep the approved identity and outfit unchanged; show the back of hair, "
                "cloak and equipment placement consistent with the reference.");
        case Direction::Left:
            return QStringLiteral(
                "VIEW: left-facing side sprite.\n"
                "Preserve body height, silhouette volume, colors and equipment placement.");
        case Direction::Right:
            return QStringLiteral(
                "VIEW: right-facing side sprite.\n"
                "Preserve body height, silhouette volume, colors and equipment placement.");
        }
        return QString();
    }

    // ---- 一致性要求（有参考图/母版时）----
    static QString consistencyDirective(bool hasReference)
    {
        if (!hasReference)
            return QStringLiteral(
                "Consistency requirements:\n"
                "- keep the design memorable and easy to reproduce in other directions");
        return QStringLiteral(
            "Use the attached reference image as the approved identity master.\n"
            "Consistency requirements:\n"
            "- preserve face shape, hairstyle, clothing structure, colors and equipment\n"
            "- change only the requested direction or pose");
    }

    // ---- 组合入口 ----
    static QString compose(const AssetSpec& spec, Direction dir)
    {
        const bool hasRef = !spec.referenceImages.isEmpty();
        switch (spec.type) {
        case AssetType::Character: {
            QStringList parts;
            parts << basePolicy(spec);
            parts << QStringLiteral("Subject: %1").arg(spec.prompt);
            parts << viewDirective(dir);
            parts << consistencyDirective(hasRef);
            return parts.join(QStringLiteral("\n\n"));
        }
        case AssetType::Item:
            return QStringLiteral(
                       "You are a production pixel-art asset generator.\n"
                       "Generate exactly one original 2D game item sprite.\n"
                       "%1\n\n"
                       "Subject: %2\n"
                       "Single item centered, plain solid background, no text, no frame.")
                .arg(basePolicy(spec), spec.prompt);
        case AssetType::Scene:
            return QStringLiteral(
                       "You are a production pixel-art asset generator.\n"
                       "Generate one original 2.5D JRPG diorama scene concept.\n"
                       "Crisp hand-crafted pixel art, layered ground/wall/decor silhouettes, "
                       "%1 key light, %2 perspective, restrained cohesive palette, no text.\n\n"
                       "Subject: %3")
                .arg(spec.styleLight, spec.stylePerspective, spec.prompt);
        }
        return spec.prompt;
    }

    // ---- 负面清单 ----
    static QString negative(AssetType type)
    {
        QStringList n;
        n << QStringLiteral("blurry") << QStringLiteral("smooth illustration")
          << QStringLiteral("vector art") << QStringLiteral("3D render")
          << QStringLiteral("anti-aliased edges") << QStringLiteral("soft brushwork")
          << QStringLiteral("photorealistic") << QStringLiteral("excessive gradients")
          << QStringLiteral("tiny unreadable detail") << QStringLiteral("cropped body")
          << QStringLiteral("floating feet") << QStringLiteral("inconsistent outfit")
          << QStringLiteral("changed hairstyle") << QStringLiteral("missing accessories")
          << QStringLiteral("extra limbs") << QStringLiteral("duplicate equipment")
          << QStringLiteral("background scenery") << QStringLiteral("floor")
          << QStringLiteral("text") << QStringLiteral("watermark")
          << QStringLiteral("sprite sheet") << QStringLiteral("multiple characters");
        if (type == AssetType::Character)
            n << QStringLiteral("multiple views in one image");
        return n.join(QStringLiteral(", "));
    }
};

} // namespace gc
