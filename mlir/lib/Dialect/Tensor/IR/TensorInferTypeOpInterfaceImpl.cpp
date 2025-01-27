//===- InferTypeOpImpl.cpp - InferType Interface external models *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Tensor/IR/TensorInferTypeOpInterfaceImpl.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/StandardOps/Utils/Utils.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"

using namespace mlir;
using namespace mlir::tensor;

/// Compute a map that for a given dimension of the expanded type gives the
/// dimension in the collapsed type it maps to. Essentially its the inverse of
/// the `reassocation` maps.
static llvm::DenseMap<int64_t, int64_t>
getExpandedDimToCollapsedDimMap(ArrayRef<AffineMap> reassociation) {
  llvm::DenseMap<int64_t, int64_t> expandedDimToCollapsedDim;
  for (const auto &map : enumerate(reassociation)) {
    unsigned startPos =
        map.value().getResults().front().cast<AffineDimExpr>().getPosition();
    unsigned endPos =
        map.value().getResults().back().cast<AffineDimExpr>().getPosition();
    for (auto dim : llvm::seq_inclusive(startPos, endPos)) {
      expandedDimToCollapsedDim[dim] = map.index();
    }
  }
  return expandedDimToCollapsedDim;
}

/// For reshape op compute the shape at dimension `dimIndex` of the output in
/// terms of shape of the `src`, when the reshape op is a collapsing
/// operation. It is the product of the shape of the collapsed dimensions of the
/// `src`.
static OpFoldResult
getCollapsedOutputDimFromInputShape(OpBuilder &builder, Location loc,
                                    int64_t dimIndex, Value src,
                                    ArrayRef<AffineMap> reassociationMap) {
  AffineMap map = reassociationMap[dimIndex];
  unsigned startPos =
      map.getResults().front().cast<AffineDimExpr>().getPosition();
  unsigned endPos = map.getResults().back().cast<AffineDimExpr>().getPosition();
  AffineExpr expr;
  SmallVector<Value, 2> dynamicDims;
  for (auto dim : llvm::seq_inclusive(startPos, endPos)) {
    dynamicDims.push_back(builder.createOrFold<tensor::DimOp>(loc, src, dim));
    AffineExpr currExpr = builder.getAffineSymbolExpr(dim - startPos);
    expr = (expr ? expr * currExpr : currExpr);
  }
  return applyMapToValues(builder, loc,
                          AffineMap::get(0, endPos - startPos + 1, expr),
                          dynamicDims)[0];
}

/// Given the `src` of a collapsing reshape op and its reassociation maps,
/// compute the shape of the result of the reshape.
static SmallVector<OpFoldResult, 4> getCollapsedOutputShapeFromInputShape(
    OpBuilder &builder, Location loc, Value src,
    ArrayRef<int64_t> dstStaticShape, ArrayRef<AffineMap> reassociation) {
  return llvm::to_vector<4>(llvm::map_range(
      llvm::seq<int64_t>(0, dstStaticShape.size()), [&](int64_t dim) {
        return getCollapsedOutputDimFromInputShape(builder, loc, dim, src,
                                                   reassociation);
      }));
}

/// For an expanding reshape op, compute the value for a dimension of the output
/// from the shape of the input.
static OpFoldResult getExpandedOutputDimFromInputShape(
    OpBuilder &builder, Location loc, int64_t dimIndex, Value src,
    ArrayRef<int64_t> dstStaticShape, ArrayRef<AffineMap> reassociation,
    llvm::DenseMap<int64_t, int64_t> &expandedDimToCollapsedDim) {
  if (!ShapedType::isDynamic(dstStaticShape[dimIndex])) {
    return builder.getI64IntegerAttr(dstStaticShape[dimIndex]);
  }
  unsigned sourceDimPos = expandedDimToCollapsedDim[dimIndex];
  unsigned startPos = reassociation[sourceDimPos]
                          .getResults()
                          .front()
                          .cast<AffineDimExpr>()
                          .getPosition();
  unsigned endPos = reassociation[sourceDimPos]
                        .getResults()
                        .back()
                        .cast<AffineDimExpr>()
                        .getPosition();
  int64_t linearizedStaticDim = 1;
  for (auto &d :
       llvm::enumerate(dstStaticShape.slice(startPos, endPos - startPos + 1))) {
    if (d.index() + startPos == static_cast<unsigned>(dimIndex))
      continue;
    assert(!ShapedType::isDynamic(d.value()) &&
           "single dimension cannot be expanded into multiple dynamic "
           "dimensions");
    linearizedStaticDim *= d.value();
  }
  Value sourceDim = builder.create<tensor::DimOp>(loc, src, sourceDimPos);
  return applyMapToValues(
      builder, loc,
      AffineMap::get(
          0, 1, builder.getAffineSymbolExpr(0).floorDiv(linearizedStaticDim)),
      sourceDim)[0];
}

/// Given the `src` of an expanding reshape op, the reassociation maps and the
/// result type, compute the shape of the result of the reshape.
static SmallVector<OpFoldResult, 4> getExpandedOutputShapeFromInputShape(
    OpBuilder &builder, Location loc, Value src,
    ArrayRef<int64_t> dstStaticShape, ArrayRef<AffineMap> reassociation) {
  llvm::DenseMap<int64_t, int64_t> expandedDimToCollapsedDim =
      getExpandedDimToCollapsedDimMap(reassociation);
  return llvm::to_vector<4>(llvm::map_range(
      llvm::seq<int64_t>(0, dstStaticShape.size()), [&](int64_t dim) {
        return getExpandedOutputDimFromInputShape(builder, loc, dim, src,
                                                  dstStaticShape, reassociation,
                                                  expandedDimToCollapsedDim);
      }));
}

static SmallVector<OpFoldResult, 4>
getReshapeOutputShapeFromInputShape(OpBuilder &builder, Location loc, Value src,
                                    ArrayRef<int64_t> dstStaticShape,
                                    ArrayRef<AffineMap> reassocation) {
  return dstStaticShape.size() >
                 static_cast<size_t>(src.getType().cast<ShapedType>().getRank())
             ? getExpandedOutputShapeFromInputShape(
                   builder, loc, src, dstStaticShape, reassocation)
             : getCollapsedOutputShapeFromInputShape(
                   builder, loc, src, dstStaticShape, reassocation);
}

/// Helper function to convert a vector of `OpFoldResult`s into a vector of
/// `Value`s.
static SmallVector<Value> getAsValues(OpBuilder &b, Location loc,
                                      ArrayRef<OpFoldResult> valueOrAttrVec) {
  return llvm::to_vector<4>(
      llvm::map_range(valueOrAttrVec, [&](OpFoldResult value) -> Value {
        return getValueOrCreateConstantIndexOp(b, loc, value);
      }));
}

template <typename OpTy>
struct ReifyExpandOrCollapseShapeOp
    : public ReifyRankedShapedTypeOpInterface::ExternalModel<
          ReifyExpandOrCollapseShapeOp<OpTy>, OpTy> {
  LogicalResult
  reifyResultShapes(Operation *op, OpBuilder &b,
                    ReifiedRankedShapedTypeDims &reifiedReturnShapes) const {
    auto loc = op->getLoc();
    auto reshape_op = cast<OpTy>(op);
    auto result_shape = getReshapeOutputShapeFromInputShape(
        b, loc, reshape_op.src(), reshape_op.getResultType().getShape(),
        reshape_op.getReassociationMaps());
    reifiedReturnShapes.push_back(getAsValues(b, loc, result_shape));
    return success();
  }
};

void mlir::tensor::registerInferTypeOpInterfaceExternalModels(
    DialectRegistry &registry) {
  registry
      .addOpInterface<tensor::ExpandShapeOp,
                      ReifyExpandOrCollapseShapeOp<tensor::ExpandShapeOp>>();
  registry
      .addOpInterface<tensor::CollapseShapeOp,
                      ReifyExpandOrCollapseShapeOp<tensor::CollapseShapeOp>>();
}
