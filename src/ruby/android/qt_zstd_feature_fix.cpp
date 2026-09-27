// qt_zstd_feature_fix.cpp — works around an aqt packaging quirk.
//
// Qt 6.6.3's rcc emits resource code that reads the data symbol
// `qt_resourceFeatureZstd` (0 = no zstd resource support, 1 = supported).
// The QtCore shipped in aqt's Android binaries was built without it, so the
// symbol is missing and linking libruby.so fails with:
//     ld.lld: error: undefined symbol: qt_resourceFeatureZstd
// The desktop (gcc_64) QtCore from the same install exports it, confirming
// this is an Android-packaging inconsistency, not an app bug.
//
// None of our .qrc files use zstd compression, so the honest value here is 0.
// If upstream/aqt ever ships an Android QtCore that exports the symbol, this
// definition must be removed (duplicate symbol) — hence the loud comment.
extern "C" const unsigned char qt_resourceFeatureZstd = 0;
