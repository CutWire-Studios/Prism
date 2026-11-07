#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <vector>

/// Colour LUT loading for the `file` param of the LUT effect.
///
/// Every LUT is normalised to one 2D atlas layout the shader can sample without knowing the
/// source size: a 512x512 image of 8x8 tiles, each tile 64x64. Tile b (row-major, b = blue index
/// 0..63) holds red along x and green along y, with row 0 at the top of the image. Samples sit at
/// texel centres, so the atlas is a 64^3 lattice.
///
/// Accepted inputs: .cube files with LUT_3D_SIZE (resampled to 64^3), a 512x512 PNG already in
/// the atlas layout, or an N^2 x N strip PNG (N tiles of NxN side by side, e.g. 1024x32).
namespace Lut {

constexpr int kSize = 64;
constexpr int kTilesPerRow = 8;
constexpr int kAtlas = kSize * kTilesPerRow;

/// Parses an Adobe/Resolve .cube 3D LUT. @p rgb gets size^3 * 3 values with red varying fastest,
/// normalised through DOMAIN_MIN/MAX to 0..1. False for 1D LUTs, short or malformed data.
bool parseCube(const QByteArray &text, int &size, std::vector<float> &rgb);

/// Resamples a size^3 lattice (same ordering as parseCube) into the 512x512 atlas.
QImage atlasFromLattice(int size, const std::vector<float> &rgb);

/// Loads a LUT file by extension/content; a null image when missing or unsupported.
QImage load(const QString &path);

} // namespace Lut
