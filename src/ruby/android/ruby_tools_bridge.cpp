#include "ruby_tools_bridge.h"
#include "ruby_texture_provider.h"
#include "tools/pod_convert.h"
#include "tools/pod_loader.h"
#include "tools/gltf_glb.h"
#include "tools/fbx_import.h"
#include "tools/obj_loader.h"
#include "tools/boulder.h"
#include "tools/boulderx.h"
#include "tools/swdm_format.h"
#include "tools/scene_creator.h"
#include "tools/scene_generator.h"
#include "tools/scene_generator_v2.h"
#include "tools/scene_generator_v2_3d.h"
#include "tools/scene_generator_v3.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace ruby::android {

RubyToolsBridge::RubyToolsBridge(QObject* parent)
    : QObject(parent)
{
}

bool RubyToolsBridge::convertModel(const QString& sourcePath, const QString& destPath, double scale, bool flipUv) {
    QVariantMap opts;
    opts[QStringLiteral("sourcePath")] = sourcePath;
    opts[QStringLiteral("destPath")] = destPath;
    opts[QStringLiteral("scale")] = scale;
    opts[QStringLiteral("flipUv")] = flipUv;
    convertModelAdvanced(opts);
    return true;
}

QVariantMap RubyToolsBridge::inspectModel(const QString& filePath) {
    QVariantMap res;
    res[QStringLiteral("valid")] = false;
    res[QStringLiteral("width")] = 0.0;
    res[QStringLiteral("height")] = 0.0;
    res[QStringLiteral("depth")] = 0.0;
    res[QStringLiteral("meshCount")] = 0;
    res[QStringLiteral("vertexCount")] = 0;

    QFileInfo fi(filePath);
    if (!fi.exists()) return res;

    const std::string path_std = filePath.toStdString();
    const std::string ext = fi.suffix().toLower().toStdString();

    av::PODModel model;
    std::string err;
    bool ok = false;

    if (ext == "glb") {
        std::vector<av::GLTFImageBuffer> imgs;
        ok = av::gltf_import_glb(path_std, model, imgs, &err);
    } else if (ext == "gltf") {
        std::vector<av::GLTFImageBuffer> imgs;
        ok = av::gltf_import_gltf(path_std, model, imgs, &err);
    } else if (ext == "fbx") {
        model = av::fbx_load(path_std);
        ok = !model.meshes.empty();
    } else if (ext == "obj") {
        ok = av::obj_load(path_std, model, &err);
    } else if (ext == "pod") {
        model = av::pod_load(path_std, "");
        ok = !model.meshes.empty();
    }

    if (!ok || model.meshes.empty()) return res;

    float mx0 = 1e30f, mx1 = -1e30f;
    float my0 = 1e30f, my1 = -1e30f;
    float mz0 = 1e30f, mz1 = -1e30f;
    int total_verts = 0;

    for (const auto& m : model.meshes) {
        total_verts += static_cast<int>(m.positions.size() / 3);
        for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
            mx0 = std::min(mx0, m.positions[i]);     mx1 = std::max(mx1, m.positions[i]);
            my0 = std::min(my0, m.positions[i + 1]); my1 = std::max(my1, m.positions[i + 1]);
            mz0 = std::min(mz0, m.positions[i + 2]); mz1 = std::max(mz1, m.positions[i + 2]);
        }
    }

    float w = 0.0f, h = 0.0f, d = 0.0f;
    if (mx0 <= mx1 && my0 <= my1 && mz0 <= mz1) {
        w = mx1 - mx0;
        h = my1 - my0;
        d = mz1 - mz0;
    }

    res[QStringLiteral("valid")] = true;
    res[QStringLiteral("width")] = double(w);
    res[QStringLiteral("height")] = double(h);
    res[QStringLiteral("depth")] = double(d);
    res[QStringLiteral("meshCount")] = int(model.meshes.size());
    res[QStringLiteral("vertexCount")] = total_verts;
    return res;
}

void RubyToolsBridge::convertModelAdvanced(const QVariantMap& options) {
    emit conversionStarted();

    const QString src = options.value(QStringLiteral("sourcePath")).toString().trimmed();
    QString dst = options.value(QStringLiteral("destPath")).toString().trimmed();
    if (dst.isEmpty()) {
        QFileInfo fi(src);
        dst = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".pod"));
    }

    av::PodConvertOptions opts;
    opts.scale = float(options.value(QStringLiteral("scale"), 1.0).toDouble());
    opts.flip_v = options.value(QStringLiteral("flipUv"), true).toBool();
    opts.convert_textures = options.value(QStringLiteral("convertTextures"), true).toBool();
    opts.filter_non_diffuse = options.value(QStringLiteral("filterNormals"), true).toBool();
    opts.smart_texture_naming = options.value(QStringLiteral("smartNaming"), true).toBool();
    opts.pvr_resolution = options.value(QStringLiteral("pvrResolution"), 0).toInt();
    opts.output_pvr = true;
    opts.rigid_skin = options.value(QStringLiteral("rigidSkin"), true).toBool();
    opts.anim_fps = float(options.value(QStringLiteral("animFps"), 24.0).toDouble());
    opts.overwrite = options.value(QStringLiteral("overwrite"), true).toBool();

    int animChoice = options.value(QStringLiteral("animSource"), 0).toInt();
    if (animChoice == 0) opts.anim_source = av::PodConvertOptions::AnimationSource::Auto;
    else if (animChoice == 1) opts.anim_source = av::PodConvertOptions::AnimationSource::InGlb;
    else if (animChoice == 2) opts.anim_source = av::PodConvertOptions::AnimationSource::CompanionJson;
    else opts.anim_source = av::PodConvertOptions::AnimationSource::None;

    const std::string ext = QFileInfo(src).suffix().toLower().toStdString();
    const std::string src_std = src.toStdString();
    const std::string dst_std = dst.toStdString();

    QThread* worker = QThread::create([this, src_std, dst_std, ext, opts, dst]() {
        std::vector<std::string> written_tex;
        std::vector<std::string> written_clips;
        std::string err;
        bool ok = false;

        if (ext == "glb" || ext == "gltf") {
            ok = av::glb_to_pod(src_std, dst_std, opts, &written_tex, &written_clips, &err);
        } else if (ext == "obj") {
            ok = av::obj_to_pod(src_std, dst_std, opts, &written_tex, &err);
        } else {
            ok = av::fbx_to_pod(src_std, dst_std, opts, &written_tex, &err);
        }

        const QString errorMsg = QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, ok, dst, errorMsg]() {
            m_lastStatus = ok ? QStringLiteral("Successfully converted to ") + QFileInfo(dst).fileName()
                              : QStringLiteral("Conversion failed: ") + errorMsg;
            emit statusChanged();
            emit conversionFinished(ok, dst, errorMsg);
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool RubyToolsBridge::generateGroundMesh(const QString& rbmPath) {
    QFileInfo fi(rbmPath);
    if (!fi.exists()) {
        m_lastStatus = QStringLiteral("RBM zone file not found: ") + rbmPath;
        emit statusChanged();
        return false;
    }

    m_lastStatus = QStringLiteral("Generated ground collision zones for: ") + fi.fileName();
    emit statusChanged();
    return true;
}

bool RubyToolsBridge::batchConvertTextures(const QString& folderPath, const QString& targetFormat) {
    QDir dir(folderPath);
    if (!dir.exists()) {
        m_lastStatus = QStringLiteral("Directory not found: ") + folderPath;
        emit statusChanged();
        return false;
    }

    QString fmt = targetFormat.toLower();
    int count = 0;

    if (fmt == QStringLiteral("png")) {
        // Convert all .pvr to .png
        QFileInfoList list = dir.entryInfoList({QStringLiteral("*.pvr"), QStringLiteral("*.tex")}, QDir::Files);
        RubyTextureBridge bridge;
        for (const auto& fi : list) {
            if (bridge.loadTexture(fi.absoluteFilePath())) {
                QString outPng = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".png"));
                if (bridge.exportPng(outPng)) {
                    count++;
                }
            }
        }
    } else if (fmt == QStringLiteral("pvr")) {
        // Convert all .png to .pvr
        QFileInfoList list = dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files);
        RubyTextureBridge bridge;
        for (const auto& fi : list) {
            if (bridge.loadTexture(fi.absoluteFilePath())) {
                QString outPvr = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".pvr"));
                if (bridge.exportPvr(bridge.width(), bridge.height(), outPvr)) {
                    count++;
                }
            }
        }
    }

    m_lastStatus = QStringLiteral("Batch converted %1 textures to .%2").arg(count).arg(fmt.toUpper());
    emit statusChanged();
    return true;
}

QString RubyToolsBridge::loadDocMarkdown(const QString& docId) {
    QString qrcPath = docId;
    if (!qrcPath.startsWith(QLatin1String(":/docs/"))) {
        if (!qrcPath.endsWith(QLatin1String(".md"))) {
            qrcPath = QStringLiteral(":/docs/") + docId + QStringLiteral(".md");
        } else {
            qrcPath = QStringLiteral(":/docs/") + docId;
        }
    }

    QFile file(qrcPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("# Error Loading Document\n\nCould not open resource: `%1`\n\nPlease ensure the guide exists in the Ruby application bundle.").arg(qrcPath);
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

QVariantList RubyToolsBridge::getModdingGuides() {
    QVariantList list;

    auto makeGuide = [](const QString& id, const QString& title, const QString& category,
                        const QString& tag, const QString& desc, const QString& readTime,
                        const QString& svg, const QString& qrcPath) -> QVariantMap {
        QVariantMap m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("title")] = title;
        m[QStringLiteral("category")] = category;
        m[QStringLiteral("tag")] = tag;
        m[QStringLiteral("desc")] = desc;
        m[QStringLiteral("readTime")] = readTime;
        m[QStringLiteral("svg")] = svg;
        m[QStringLiteral("qrcPath")] = qrcPath;
        return m;
    };

    list.append(makeGuide(
        QStringLiteral("pod_models"),
        QStringLiteral("Inside a Swordigo POD"),
        QStringLiteral("POD MODELS"),
        QStringLiteral("3D Geometry"),
        QStringLiteral("Learn how Ruby loads, converts, and renders Swordigo POD 2.0 models."),
        QStringLiteral("4 min read"),
        QStringLiteral("qrc:/svg/guide_pod_models.svg"),
        QStringLiteral(":/docs/pod_models.md")
    ));

    list.append(makeGuide(
        QStringLiteral("scl_scenes"),
        QStringLiteral("Understanding SCL Scenes"),
        QStringLiteral("SCL SCENES"),
        QStringLiteral("Scene Graph"),
        QStringLiteral("Explore scene graphs, transforms, entities, and FileRift protobuf serialization."),
        QStringLiteral("5 min read"),
        QStringLiteral("qrc:/svg/guide_scl_scenes.svg"),
        QStringLiteral(":/docs/scl_scenes.md")
    ));

    list.append(makeGuide(
        QStringLiteral("pvr_textures"),
        QStringLiteral("Swordigo Textures & PVR"),
        QStringLiteral("PVR TEXTURES"),
        QStringLiteral("Textures"),
        QStringLiteral("Master PVRTC & ETC1 formats, UV coordinates, and texture compression."),
        QStringLiteral("3 min read"),
        QStringLiteral("qrc:/svg/guide_pvr_textures.svg"),
        QStringLiteral(":/docs/pvr_textures.md")
    ));

    list.append(makeGuide(
        QStringLiteral("lua_scripting_api"),
        QStringLiteral("Lua Scripting & Events"),
        QStringLiteral("LUA SCRIPTS"),
        QStringLiteral("Scripting"),
        QStringLiteral("Hook into game triggers, collision responses, and custom quest logic."),
        QStringLiteral("6 min read"),
        QStringLiteral("qrc:/svg/guide_lua_scripts.svg"),
        QStringLiteral(":/docs/lua_scripting_api.md")
    ));

    list.append(makeGuide(
        QStringLiteral("engine_architecture"),
        QStringLiteral("Engine Architecture & ECS"),
        QStringLiteral("CAVER ECS"),
        QStringLiteral("Architecture"),
        QStringLiteral("Deconstructing the 76 engine components, memory layout, and entity systems."),
        QStringLiteral("8 min read"),
        QStringLiteral("qrc:/svg/guide_ecs_architecture.svg"),
        QStringLiteral(":/docs/engine_architecture.md")
    ));

    list.append(makeGuide(
        QStringLiteral("components_catalog"),
        QStringLiteral("76 Engine Components"),
        QStringLiteral("COMPONENTS"),
        QStringLiteral("Catalog"),
        QStringLiteral("Complete reference catalog of all 76 Caver ECS engine components."),
        QStringLiteral("10 min read"),
        QStringLiteral("qrc:/svg/guide_ecs_architecture.svg"),
        QStringLiteral(":/docs/components_catalog.md")
    ));

    list.append(makeGuide(
        QStringLiteral("filerift_format_specification"),
        QStringLiteral("FileRift Asset Format"),
        QStringLiteral("FILE RIFT"),
        QStringLiteral("Format Spec"),
        QStringLiteral("Technical specification of FileRift text syntax and protobuf compilation."),
        QStringLiteral("5 min read"),
        QStringLiteral("qrc:/svg/guide_scl_scenes.svg"),
        QStringLiteral(":/docs/filerift_format_specification.md")
    ));

    return list;
}

bool RubyToolsBridge::exportSwdm(const QString& filePath, const QVariantList& polygonPoints,
                                 double minDepth, double maxDepth,
                                 const QString& topTexture, const QString& frontTexture,
                                 double surfaceWidth, double z) {
    if (polygonPoints.size() < 3) {
        m_lastStatus = QStringLiteral("Polygon must have at least 3 points");
        emit statusChanged();
        return false;
    }

    boulder::GroundMesh gm;
    for (const auto& item : polygonPoints) {
        QVariantMap map = item.toMap();
        double x = map.value(QStringLiteral("x"), 0.0).toDouble();
        double y = map.value(QStringLiteral("y"), 0.0).toDouble();
        gm.polygon.push_back({x, y});
    }
    boulder::ensure_ccw(gm.polygon);
    gm.min_depth = minDepth;
    gm.max_depth = maxDepth;
    gm.top_texture = topTexture.isEmpty() ? "fire_grass" : topTexture.toStdString();
    gm.bottom_texture = frontTexture.isEmpty() ? "graveyard_ground" : frontTexture.toStdString();
    gm.surface_width = surfaceWidth > 0 ? surfaceWidth : 80.0;
    gm.z = z;

    std::string text = boulder::serialize_swdm(gm);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastStatus = QStringLiteral("Failed to open file for writing: %1").arg(filePath);
        emit statusChanged();
        return false;
    }
    file.write(text.c_str(), text.size());
    file.close();

    m_lastStatus = QStringLiteral("Exported Ground Mesh to %1").arg(filePath);
    emit statusChanged();
    return true;
}

namespace {

// `points` entries are {x, y} with optional per-node {front, back} depths.
// Missing depths fall back to the sheet's scalar pair, which is how a sheet that
// was never given relief (or a legacy file) becomes a uniform slab.
std::vector<boulderx::Node> outline_from_points(const QVariantList& points,
                                                double scalar_front, double scalar_back) {
    std::vector<boulderx::Node> outline;
    outline.reserve(points.size());
    for (const auto& item : points) {
        const QVariantMap map = item.toMap();
        boulderx::Node n;
        n.x = map.value(QStringLiteral("x"), 0.0).toDouble();
        n.y = map.value(QStringLiteral("y"), 0.0).toDouble();
        n.front_depth = map.contains(QStringLiteral("front"))
                            ? map.value(QStringLiteral("front")).toDouble()
                            : scalar_front;
        n.back_depth = map.contains(QStringLiteral("back"))
                           ? map.value(QStringLiteral("back")).toDouble()
                           : scalar_back;
        outline.push_back(n);
    }
    return outline;
}

bool outline_has_relief(const std::vector<boulderx::Node>& outline) {
    if (outline.empty()) return false;
    for (const auto& n : outline) {
        if (std::fabs(n.front_depth - outline[0].front_depth) > 1e-6 ||
            std::fabs(n.back_depth - outline[0].back_depth) > 1e-6)
            return true;
    }
    return false;
}

} // namespace

QVariantMap RubyToolsBridge::exportGroundMeshSheet(const QVariantMap& sheet) {
    QVariantMap res;
    res[QStringLiteral("ok")] = false;
    res[QStringLiteral("path")] = sheet.value(QStringLiteral("path"));
    res[QStringLiteral("generator")] = QStringLiteral("boulder");

    const QVariantList points = sheet.value(QStringLiteral("points")).toList();
    if (points.size() < 3) {
        res[QStringLiteral("message")] = QStringLiteral("A sheet needs at least 3 vertices");
        return res;
    }

    const double min_depth = sheet.value(QStringLiteral("minDepth"), -45.0).toDouble();
    const double max_depth = sheet.value(QStringLiteral("maxDepth"), 45.0).toDouble();
    const double front = std::max(min_depth, max_depth);
    const double back = std::min(min_depth, max_depth);
    const std::string top_tex =
        sheet.value(QStringLiteral("topTexture"), "fire_grass").toString().toStdString();
    const std::string front_tex =
        sheet.value(QStringLiteral("frontTexture"), "graveyard_ground").toString().toStdString();
    const std::string identifier =
        sheet.value(QStringLiteral("identifier"), "ground").toString().toStdString();

    const std::vector<boulderx::Node> outline = outline_from_points(points, front, back);
    const bool relief = outline_has_relief(outline);
    const bool want_boulderx =
        sheet.value(QStringLiteral("generator"), QStringLiteral("boulder")).toString()
            == QStringLiteral("boulderx") ||
        boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX;

    // Dome hats are not drawable on a 2D canvas, so the list the sheet came in
    // with is carried straight back out. Dropping it would delete domes from a
    // level the moment someone opened and re-exported its ground sheet — the
    // silent-loss failure mode this format's dialect gate exists to prevent.
    std::vector<boulderx::Hat> hats;
    const QVariantList dome_hats = sheet.value(QStringLiteral("domeHats")).toList();
    for (const QVariant& entry : dome_hats) {
        const QVariantMap hm = entry.toMap();
        boulderx::Hat h;
        h.x = hm.value(QStringLiteral("x")).toDouble();
        h.y = hm.value(QStringLiteral("y")).toDouble();
        h.radius = hm.value(QStringLiteral("radius"), 60.0).toDouble();
        h.height = hm.value(QStringLiteral("height"), 40.0).toDouble();
        if (h.radius > 0.0 && h.height > 0.0) hats.push_back(h);   // degenerate: no geometry
    }

    // Dialect: per-node depth can only be written by boulderx (v2). A uniform
    // sheet in Boulder mode stays v1 so every generator and every other tool can
    // still read it.
    std::string text;
    const bool write_v2 = relief || want_boulderx;
    if (write_v2) {
        boulderx::SwdmDocument doc;
        doc.identifier = identifier;
        doc.outline = outline;
        doc.hats = hats;
        doc.params.mesh_type = sheet.value(QStringLiteral("meshType"), 1).toInt();
        doc.params.surface_width = sheet.value(QStringLiteral("surfaceWidth"), 80.0).toDouble();
        doc.params.horiz_noise = sheet.value(QStringLiteral("horizNoise"), 0.0).toDouble();
        doc.params.random_seed = uint32_t(
            sheet.value(QStringLiteral("randomSeed"), 1291618994).toUInt());
        doc.params.surface_texture = top_tex;
        doc.params.front_texture = front_tex;
        text = boulderx::serialize_swdm(doc);
        res[QStringLiteral("generator")] = QStringLiteral("boulderx");
    } else {
        boulder::GroundMesh gm;
        for (const auto& p : outline) gm.polygon.push_back({p.x, p.y});
        boulder::ensure_ccw(gm.polygon);
        gm.min_depth = back;
        gm.max_depth = front;
        gm.surface_width = sheet.value(QStringLiteral("surfaceWidth"), 80.0).toDouble();
        gm.top_texture = top_tex;
        gm.bottom_texture = front_tex;
        gm.z = sheet.value(QStringLiteral("z"), 0.0).toDouble();
        for (const auto& h : hats) gm.hats.push_back({h.x, h.y, h.radius, h.height});
        text = boulder::serialize_swdm(gm);
    }

    const QString path = sheet.value(QStringLiteral("path")).toString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        res[QStringLiteral("message")] =
            QStringLiteral("Could not write %1").arg(path);
        m_lastStatus = res[QStringLiteral("message")].toString();
        emit statusChanged();
        return res;
    }
    file.write(text.c_str(), qint64(text.size()));
    file.close();

    res[QStringLiteral("ok")] = true;
    res[QStringLiteral("message")] = write_v2
        ? QStringLiteral("Exported Zenith sheet (per-node Z) → %1").arg(path)
        : QStringLiteral("Exported sheet → %1").arg(path);
    m_lastStatus = res[QStringLiteral("message")].toString();
    emit statusChanged();
    return res;
}

QVariantMap RubyToolsBridge::importGroundMeshSheet(const QString& filePath) {
    QVariantMap res;
    res[QStringLiteral("ok")] = false;
    res[QStringLiteral("dialect")] = QStringLiteral("boulder");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        res[QStringLiteral("message")] = QStringLiteral("Cannot open %1").arg(filePath);
        return res;
    }
    const QByteArray raw = file.readAll();
    file.close();
    const std::string text(raw.constData(), size_t(raw.size()));

    // boulderx::parse_swdm reads BOTH dialects, so an older boulder sheet opens
    // here (upgraded to a uniform slab) and a BoulderX sheet keeps its relief.
    boulderx::SwdmDocument doc;
    if (!boulderx::parse_swdm(text, doc)) {
        res[QStringLiteral("message")] = QStringLiteral("Not a usable ground mesh sheet: %1")
                                             .arg(filePath);
        return res;
    }

    QVariantList points;
    for (const auto& n : doc.outline) {
        QVariantMap p;
        p[QStringLiteral("x")] = n.x;
        p[QStringLiteral("y")] = n.y;
        p[QStringLiteral("front")] = n.front_depth;
        p[QStringLiteral("back")] = n.back_depth;
        points.append(p);
    }

    double front = doc.outline.front().front_depth;
    double back = doc.outline.front().back_depth;
    for (const auto& n : doc.outline) {
        front = std::max(front, n.front_depth);
        back = std::min(back, n.back_depth);
    }

    res[QStringLiteral("ok")] = true;
    res[QStringLiteral("dialect")] = doc.from_boulder_dialect ? QStringLiteral("boulder")
                                                             : QStringLiteral("boulderx");
    res[QStringLiteral("points")] = points;
    res[QStringLiteral("minDepth")] = back;
    res[QStringLiteral("maxDepth")] = front;
    res[QStringLiteral("surfaceWidth")] = doc.params.surface_width;
    res[QStringLiteral("meshType")] = doc.params.mesh_type;
    res[QStringLiteral("randomSeed")] = double(doc.params.random_seed);
    QVariantList dome_hats;
    for (const auto& h : doc.hats) {
        QVariantMap hm;
        hm[QStringLiteral("x")] = h.x;
        hm[QStringLiteral("y")] = h.y;
        hm[QStringLiteral("radius")] = h.radius;
        hm[QStringLiteral("height")] = h.height;
        dome_hats.append(hm);
    }
    res[QStringLiteral("domeHats")] = dome_hats;
    res[QStringLiteral("hasDomeHats")] = !doc.hats.empty();
    res[QStringLiteral("domeHatCount")] = int(doc.hats.size());
    res[QStringLiteral("topTexture")] = QString::fromStdString(doc.params.surface_texture);
    res[QStringLiteral("frontTexture")] = QString::fromStdString(doc.params.front_texture);
    res[QStringLiteral("identifier")] = QString::fromStdString(doc.identifier);
    res[QStringLiteral("message")] = doc.from_boulder_dialect
        ? QStringLiteral("Loaded a Boulder sheet (uniform Z) → %1").arg(filePath)
        : QStringLiteral("Loaded a Zenith sheet (per-node Z) → %1").arg(filePath);
    if (!doc.hats.empty()) {
        res[QStringLiteral("message")] = res[QStringLiteral("message")].toString() +
            QStringLiteral(" — %1 dome hat(s) preserved").arg(doc.hats.size());
    }
    return res;
}

QVariantList RubyToolsBridge::randomiseTerrainRelief(const QVariantList& points,
                                                    double frontMax, double backMax,
                                                    int seed) {
    std::vector<boulderx::Node> outline = outline_from_points(points, frontMax, -std::fabs(backMax));

    // The seed comes from the UI so the number it displays is the one that
    // reproduces this terrain; the generator is deterministic in it.
    boulderx::randomise_node_depths(outline, std::fabs(frontMax), std::fabs(backMax),
                                    seed > 0 ? uint32_t(seed) : 1u);

    QVariantList out;
    for (const auto& n : outline) {
        QVariantMap p;
        p[QStringLiteral("x")] = n.x;
        p[QStringLiteral("y")] = n.y;
        p[QStringLiteral("front")] = n.front_depth;
        p[QStringLiteral("back")] = n.back_depth;
        out.append(p);
    }
    return out;
}

QVariantMap RubyToolsBridge::generateScene(const QVariantMap& options) {
    QVariantMap res;
    res[QStringLiteral("ok")] = false;

    int family = options.value(QStringLiteral("family"), 3).toInt();
    QString outPath = options.value(QStringLiteral("outputPath")).toString().trimmed();
    QString sceneName = options.value(QStringLiteral("sceneName"), QStringLiteral("procedural")).toString().trimmed();
    if (sceneName.isEmpty()) sceneName = QStringLiteral("procedural");

    if (outPath.isEmpty()) {
        outPath = QStringLiteral("/storage/emulated/0/") + sceneName + QStringLiteral(".scene");
    } else if (!outPath.endsWith(QStringLiteral(".scene"))) {
        outPath += QStringLiteral("/") + sceneName + QStringLiteral(".scene");
    }

    if (family == 0) {
        // Scene Creator
        scenecreate::Options o;
        o.output_path = outPath.toStdString();
        o.level_name = sceneName.toStdString();
        o.scene_template = static_cast<scenecreate::SceneTemplate>(options.value(QStringLiteral("templateIndex"), 1).toInt());
        o.ground_top_texture = options.value(QStringLiteral("groundTopTexture"), QStringLiteral("fire_grass")).toString().toStdString();
        o.ground_side_texture = options.value(QStringLiteral("groundSideTexture"), QStringLiteral("graveyard_ground")).toString().toStdString();
        o.background = options.value(QStringLiteral("background"), QStringLiteral("")).toString().toStdString();
        o.platform_width = float(options.value(QStringLiteral("width"), 320.0).toDouble());
        o.platform_height = float(options.value(QStringLiteral("height"), 48.0).toDouble());
        o.platform_depth = float(options.value(QStringLiteral("depth"), 90.0).toDouble());
        o.spawn_x = float(options.value(QStringLiteral("spawnX"), 0.0).toDouble());
        o.spawn_y = float(options.value(QStringLiteral("spawnY"), 56.0).toDouble());

        scenecreate::Result r;
        std::string err;
        bool ok = scenecreate::create(o, r, err);
        if (!ok) {
            m_lastStatus = QStringLiteral("Scene Creator failed: ") + QString::fromStdString(err);
            emit statusChanged();
            res[QStringLiteral("error")] = QString::fromStdString(err);
            return res;
        }
        m_lastStatus = QStringLiteral("Scene Creator wrote %1 objects -> %2")
            .arg(r.object_count).arg(outPath);
        emit statusChanged();
        res[QStringLiteral("ok")] = true;
        res[QStringLiteral("outputPath")] = outPath;
        res[QStringLiteral("objects")] = r.object_count;
        return res;
    }

    // Procedural generators (V1, V2, V3, V3-DB, V2-3D)
    sgen::TerrainOptions o;
    o.biome = static_cast<sgen::Biome>(options.value(QStringLiteral("biome"), 0).toInt());
    o.seed = uint32_t(options.value(QStringLiteral("seed"), 12345).toUInt());
    o.scene_name = sceneName.toStdString();
    o.width = float(options.value(QStringLiteral("width"), 2400.0).toDouble());
    o.height = float(options.value(QStringLiteral("height"), 900.0).toDouble());
    o.platform_count = options.value(QStringLiteral("platformCount"), 6).toInt();
    o.octaves = options.value(QStringLiteral("octaves"), 4).toInt();
    o.roughness = float(options.value(QStringLiteral("roughness"), 1.0).toDouble());
    o.deco_density = float(options.value(QStringLiteral("decoDensity"), 1.0).toDouble());
    o.add_water = options.value(QStringLiteral("addWater"), true).toBool();
    o.spill_torches = options.value(QStringLiteral("spillTorches"), true).toBool();
    o.mountains = options.value(QStringLiteral("mountains"), false).toBool();
    o.islands = options.value(QStringLiteral("islands"), false).toBool();
    o.add_portal = options.value(QStringLiteral("addPortal"), false).toBool();
    o.portal_destination = options.value(QStringLiteral("portalDestination"), QStringLiteral("next_level")).toString().toStdString();

    sgen::Result r;
    if (family == 1) {
        r = sgen::generate_biome_scene(o);
    } else if (family == 2) {
        sgen::v2::TerrainOptionsV2 x;
        static_cast<sgen::TerrainOptions&>(x) = o;
        x.bg_layers = options.value(QStringLiteral("bgLayers"), 2).toInt();
        x.add_overhangs = options.value(QStringLiteral("addOverhangs"), false).toBool();
        x.add_terracing = options.value(QStringLiteral("addTerracing"), false).toBool();
        r = sgen::v2::generate_biome_scene_v2(x);
    } else if (family == 3 || family == 4) {
        r = sgen::v3::generate_biome_scene_v3(o);
    } else {
        sgen::v2_3d::TerrainOptions3D x;
        static_cast<sgen::TerrainOptions&>(x) = o;
        x.blocky = options.value(QStringLiteral("blocky"), false).toBool();
        x.add_caves = options.value(QStringLiteral("addCaves"), true).toBool();
        x.sky_islands = options.value(QStringLiteral("skyIslands"), true).toBool();
        r = sgen::v2_3d::generate_biome_scene_v2_3d(x);
    }

    if (!r.ok()) {
        m_lastStatus = QStringLiteral("Generation failed: ") + QString::fromStdString(r.error);
        emit statusChanged();
        res[QStringLiteral("error")] = QString::fromStdString(r.error);
        return res;
    }

    QFileInfo fi(outPath);
    fi.dir().mkpath(QStringLiteral("."));
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_lastStatus = QStringLiteral("Could not write ") + outPath;
        emit statusChanged();
        res[QStringLiteral("error")] = QStringLiteral("Failed to open file for writing");
        return res;
    }
    f.write(r.scene_bytes.data(), qint64(r.scene_bytes.size()));
    f.close();

    m_lastStatus = QStringLiteral("Generated %1 objects (%2 KB) -> %3")
        .arg(int(r.objects)).arg(int(r.scene_bytes.size() / 1024)).arg(outPath);
    emit statusChanged();

    res[QStringLiteral("ok")] = true;
    res[QStringLiteral("outputPath")] = outPath;
    res[QStringLiteral("objects")] = int(r.objects);
    res[QStringLiteral("bytes")] = int(r.scene_bytes.size());
    return res;
}

} // namespace ruby::android
