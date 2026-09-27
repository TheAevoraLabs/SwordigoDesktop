#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace ruby::android {

class RubyToolsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastStatusMessage READ lastStatusMessage NOTIFY statusChanged)

public:
    explicit RubyToolsBridge(QObject* parent = nullptr);

    QString lastStatusMessage() const { return m_lastStatus; }

    Q_INVOKABLE bool convertModel(const QString& sourcePath, const QString& destPath, double scale, bool flipUv);
    Q_INVOKABLE QVariantMap inspectModel(const QString& filePath);
    Q_INVOKABLE void convertModelAdvanced(const QVariantMap& options);
    Q_INVOKABLE bool generateGroundMesh(const QString& rbmPath);
    Q_INVOKABLE bool batchConvertTextures(const QString& folderPath, const QString& targetFormat);
    Q_INVOKABLE QVariantMap generateScene(const QVariantMap& options);
    Q_INVOKABLE bool exportSwdm(const QString& filePath, const QVariantList& polygonPoints,
                                double minDepth, double maxDepth,
                                const QString& topTexture, const QString& frontTexture,
                                double surfaceWidth, double z);

    // ── Ground mesh sheets (.swdm) ──────────────────────────────────────────
    // The two entry points below are the studio's whole sheet contract, and the
    // sheet DIALECT is decided here rather than in QML:
    //
    //   * a sheet with one depth per node — or one authored with the BoulderX
    //     generator selected — is written as format v2 (boulderx). Boulder cannot
    //     represent per-node depth, so it refuses those files. That is the
    //     one-way compatibility the format is defined by;
    //   * a uniform sheet authored in Boulder mode stays v1, which both
    //     generators read.
    //
    // `sheet` keys: path, points ({x, y, front, back}), minDepth, maxDepth,
    // surfaceWidth, topTexture, frontTexture, z, meshType, randomSeed,
    // horizNoise, generator ("boulder" | "boulderx"), identifier.
    // Returns: ok, path, generator ("boulder" | "boulderx", i.e. what the file
    // actually is), message.
    Q_INVOKABLE QVariantMap exportGroundMeshSheet(const QVariantMap& sheet);

    // Reads EITHER dialect back. A v1 sheet becomes a uniform slab at its own
    // depth pair, a v2 sheet keeps its per-node relief, so an old sheet opens in
    // BoulderX without losing anything and a BoulderX sheet opens with its Z
    // intact. This is the mirror of exportGroundMeshSheet's refusal rule.
    // Returns: ok, dialect ("boulderx" | "boulder"), points ({x, y, front,
    // back}), minDepth, maxDepth, surfaceWidth, topTexture, frontTexture,
    // meshType, randomSeed, identifier, hasDomeHats, message.
    Q_INVOKABLE QVariantMap importGroundMeshSheet(const QString& filePath);

    // One "randomise terrain Z" pass, over the same generator the editors use
    // (boulderx::randomise_node_depths), so the studio and the in-scene mesh
    // editor produce identical relief for the same input and seed instead of two
    // different randoms. `points` are {x, y[, front, back]} maps; the result adds
    // `front` / `back` per node.
    //
    // The seed is owned by the CALLER: it is what the UI shows, and re-entering
    // it must reproduce this terrain. seed <= 0 is normalised to 1 rather than
    // drawing a hidden random one, so an unseeded call is still reproducible.
    Q_INVOKABLE QVariantList randomiseTerrainRelief(const QVariantList& points,
                                                    double frontMax, double backMax,
                                                    int seed);

    // #will be unlocked after — mirrors boulderx::kPerNodeReliefUnlocked so the
    // QML studios can DISABLE and label the relief controls rather than let a
    // tap come back flat with no explanation. True means per-node Z is staged
    // off for this release and a sheet ships as a uniform slab.
    Q_INVOKABLE bool perNodeReliefUnlocked() const;

    // Documentation & Modding Guides
    Q_INVOKABLE QString loadDocMarkdown(const QString& docId);
    Q_INVOKABLE QVariantList getModdingGuides();

signals:
    void statusChanged();
    void conversionStarted();
    void conversionFinished(bool success, const QString& outputPath, const QString& errorMessage);

private:
    QString m_lastStatus;
};

} // namespace ruby::android
