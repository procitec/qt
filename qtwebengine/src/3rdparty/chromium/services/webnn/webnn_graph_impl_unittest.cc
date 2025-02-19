// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/webnn/webnn_graph_impl.h"

#include <limits>

#include "base/containers/contains.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/ml/webnn/features.mojom-features.h"
#include "components/ml/webnn/graph_validation_utils.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/functions.h"
#include "services/webnn/public/mojom/webnn_context_provider.mojom.h"
#include "services/webnn/public/mojom/webnn_graph.mojom.h"
#include "services/webnn/webnn_context_impl.h"
#include "services/webnn/webnn_context_provider_impl.h"
#include "services/webnn/webnn_test_utils.h"
#include "services/webnn/webnn_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace webnn {

namespace {

// A fake WebNNGraph Mojo interface implementation that binds a pipe for
// computing graph message.
class FakeWebNNGraphImpl final : public WebNNGraphImpl {
 public:
  explicit FakeWebNNGraphImpl(ComputeResourceInfo compute_resource_info)
      : WebNNGraphImpl(std::move(compute_resource_info)) {}
  ~FakeWebNNGraphImpl() override = default;

  static void CreateAndBuild(
      const mojom::GraphInfoPtr& graph_info,
      mojom::WebNNContext::CreateGraphCallback callback) {
    mojo::PendingRemote<mojom::WebNNGraph> blink_remote;
    // The receiver bound to FakeWebNNGraphImpl.
    mojo::MakeSelfOwnedReceiver<mojom::WebNNGraph>(
        std::make_unique<FakeWebNNGraphImpl>(ComputeResourceInfo(graph_info)),
        blink_remote.InitWithNewPipeAndPassReceiver());
    std::move(callback).Run(
        mojom::CreateGraphResult::NewGraphRemote(std::move(blink_remote)));
  }

 private:
  // Return the `kOk` result for testing the validation of inputs and outputs in
  // `WebNNGraphImpl::Compute()` function.
  void ComputeImpl(base::flat_map<std::string, mojo_base::BigBuffer> inputs,
                   mojom::WebNNGraph::ComputeCallback callback) override {
    base::flat_map<std::string, mojo_base::BigBuffer> named_outputs;
    std::move(callback).Run(
        mojom::ComputeResult::NewNamedOutputs(std::move(named_outputs)));
  }
};

// A fake WebNNContext Mojo interface implementation that binds a pipe for
// creating graph message.
class FakeWebNNContextImpl final : public WebNNContextImpl {
 public:
  FakeWebNNContextImpl(mojo::PendingReceiver<mojom::WebNNContext> receiver,
                       WebNNContextProviderImpl* context_provider)
      : WebNNContextImpl(std::move(receiver), context_provider) {}
  ~FakeWebNNContextImpl() override = default;

 private:
  void CreateGraphImpl(
      mojom::GraphInfoPtr graph_info,
      mojom::WebNNContext::CreateGraphCallback callback) override {
    FakeWebNNGraphImpl::CreateAndBuild(std::move(graph_info),
                                       std::move(callback));
  }
};

// Helper class to create the FakeWebNNContext that is intended to test
// the graph validation steps and computation resources.
class FakeWebNNBackend : public WebNNContextProviderImpl::BackendForTesting {
 public:
  void CreateWebNNContext(
      std::vector<std::unique_ptr<WebNNContextImpl>>& context_impls,
      WebNNContextProviderImpl* context_provider_impl,
      mojom::CreateContextOptionsPtr options,
      mojom::WebNNContextProvider::CreateWebNNContextCallback callback)
      override {
    mojo::PendingRemote<mojom::WebNNContext> blink_remote;
    // The receiver bound to FakeWebNNContext.
    context_impls.push_back(std::make_unique<FakeWebNNContextImpl>(
        blink_remote.InitWithNewPipeAndPassReceiver(), context_provider_impl));
    std::move(callback).Run(
        mojom::CreateContextResult::NewContextRemote(std::move(blink_remote)));
  }
};

bool ValidateInputsForComputing(
    mojom::GraphInfoPtr graph_info,
    base::flat_map<std::string, mojo_base::BigBuffer> inputs) {
  // Creates WebNN Context mojo interface with the provider.
  mojo::Remote<mojom::WebNNContextProvider> provider_remote;
  WebNNContextProviderImpl::Create(
      provider_remote.BindNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::CreateContextResultPtr> create_context_future;
  provider_remote->CreateWebNNContext(mojom::CreateContextOptions::New(),
                                      create_context_future.GetCallback());
  mojom::CreateContextResultPtr create_context_result =
      create_context_future.Take();
  mojo::Remote<mojom::WebNNContext> webnn_context;
  webnn_context.Bind(std::move(create_context_result->get_context_remote()));

  // Creates WebNN Graph mojo interface with the graph information which is
  // validated before compiling.
  base::test::TestFuture<mojom::CreateGraphResultPtr> create_graph_future;
  webnn_context->CreateGraph(std::move(graph_info),
                             create_graph_future.GetCallback());
  mojom::CreateGraphResultPtr create_graph_result = create_graph_future.Take();
  mojo::Remote<mojom::WebNNGraph> webnn_graph;
  webnn_graph.Bind(std::move(create_graph_result->get_graph_remote()));

  // Validate the inputs in the `Compute` function.
  bool valid = true;
  // Set up the error handler for bad mojo messages.
  mojo::SetDefaultProcessErrorHandler(
      base::BindLambdaForTesting([&](const std::string& error_message) {
        EXPECT_EQ(error_message,
                  "The inputs for computation don't match the built graph's "
                  "expectation.");
        valid = false;
      }));

  base::test::TestFuture<mojom::ComputeResultPtr> compute_future;
  webnn_graph->Compute(std::move(inputs), compute_future.GetCallback());
  EXPECT_TRUE(compute_future.Wait());

  mojo::SetDefaultProcessErrorHandler(base::NullCallback());
  return valid;
}

mojom::Operand::DataType kAllOperandDataTypes[] = {
    mojom::Operand::DataType::kFloat32, mojom::Operand::DataType::kFloat16,
    mojom::Operand::DataType::kInt32,   mojom::Operand::DataType::kInt8,
    mojom::Operand::DataType::kUint8,
};

}  // namespace

class WebNNGraphImplTest : public testing::Test {
 public:
  WebNNGraphImplTest(const WebNNGraphImplTest&) = delete;
  WebNNGraphImplTest& operator=(const WebNNGraphImplTest&) = delete;

  void SetUp() override {
    WebNNContextProviderImpl::SetBackendForTesting(&backend_for_testing_);
  }
  void TearDown() override {
    WebNNContextProviderImpl::SetBackendForTesting(nullptr);
  }

 protected:
  WebNNGraphImplTest()
      : scoped_feature_list_(
            webnn::mojom::features::kWebMachineLearningNeuralNetwork) {}
  ~WebNNGraphImplTest() override = default;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  base::test::TaskEnvironment task_environment_;

  FakeWebNNBackend backend_for_testing_;
};

struct OperandInfo {
  mojom::Operand::DataType type;
  std::vector<uint32_t> dimensions;
};

struct ArgMinMaxTester {
  mojom::ArgMinMax::Kind kind;
  OperandInfo input;
  std::vector<uint32_t> axes;
  bool keep_dimensions = false;
  bool select_last_index = false;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildArgMinMax(kind, input_operand_id, output_operand_id, axes,
                           keep_dimensions, select_last_index);

    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ArgMinMaxTest) {
  const auto ArgMinMaxKinds = {mojom::ArgMinMax_Kind::kMin,
                               mojom::ArgMinMax_Kind::kMax};
  for (const auto kind : ArgMinMaxKinds) {
    {
      // Test argMinMax operator with axis = {0} and keep_dimensions = true.
      ArgMinMaxTester{.kind = kind,
                      .input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {0},
                      .keep_dimensions = true,
                      .output = {.type = mojom::Operand::DataType::kInt64,
                                 .dimensions = {1, 3, 4, 5}},
                      .expected = true}
          .Test();
    }
    {
      // Test argMinMax operator with axis = {0, 1} and keep_dimensions = false.
      ArgMinMaxTester{.kind = kind,
                      .input = {.type = mojom::Operand::DataType::kFloat16,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {0, 1},
                      .keep_dimensions = false,
                      .output = {.type = mojom::Operand::DataType::kInt64,
                                 .dimensions = {4, 5}},
                      .expected = true}
          .Test();
    }
    {
      // Test the invalid graph when value in the axes sequence is greater than
      // or equal to input rank.
      ArgMinMaxTester{.kind = kind,
                      .input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {4},
                      .keep_dimensions = true,
                      .output = {.type = mojom::Operand::DataType::kInt64,
                                 .dimensions = {2, 3, 4, 1}},
                      .expected = false}
          .Test();
    }
    {
      // Test the invalid graph when two or more values are same in the axes
      // sequence.
      ArgMinMaxTester{.kind = mojom::ArgMinMax::Kind::kMax,
                      .input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {1, 1},
                      .keep_dimensions = true,
                      .output = {.type = mojom::Operand::DataType::kInt64,
                                 .dimensions = {1, 3, 4, 5}},
                      .expected = false}
          .Test();
    }
    {
      // Test the invalid graph when the output data type is not support.
      ArgMinMaxTester{.kind = kind,
                      .input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {0},
                      .keep_dimensions = true,
                      .output = {.type = mojom::Operand::DataType::kFloat32,
                                 .dimensions = {1, 3, 4, 5}},
                      .expected = false}
          .Test();
    }
    {
      // Test the invalid graph when the output shape is incorrect.
      ArgMinMaxTester{.kind = kind,
                      .input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4, 5}},
                      .axes = {0},
                      .keep_dimensions = false,
                      .output = {.type = mojom::Operand::DataType::kInt64,
                                 .dimensions = {1, 3, 4, 5}},
                      .expected = false}
          .Test();
    }
    {
      // Test the invalid graph when the input and output are same operand.
      GraphInfoBuilder builder;
      uint64_t input_operand_id = builder.BuildInput(
          "input", {2, 3, 4, 5}, mojom::Operand::DataType::kInt64);
      builder.BuildArgMinMax(kind, input_operand_id, input_operand_id, {0},
                             true, false);
      EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
    }
  }
}

struct ClampTester {
  OperandInfo input;
  struct ClampAttributes {
    float min_value;
    float max_value;
  };
  ClampAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildClamp(input_operand_id, output_operand_id,
                       attributes.min_value, attributes.max_value);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ClampTest) {
  {
    // Test clamp operator with both the minimum and maximum values.
    ClampTester{.input = {.type = mojom::Operand::DataType::kInt8,
                          .dimensions = {3, 4}},
                .attributes = {.min_value = 0.0, .max_value = 6.0},
                .output = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {3, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test clamp operator with the min value is infinite.
    ClampTester{.input = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {2, 3, 4}},
                .attributes = {.min_value = static_cast<float>(-1.0 / 0.0),
                               .max_value = 3.0},
                .output = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {2, 3, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test clamp operator with the max value is infinite.
    ClampTester{.input = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {2, 3, 4}},
                .attributes = {.min_value = 0.0,
                               .max_value = static_cast<float>(1.0 / 0.0)},
                .output = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {2, 3, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when max value = 0 and min value = 0.
    ClampTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {1, 2, 2, 7}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 2, 2, 7}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the max value is less than the min value.
    ClampTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 2}},
                .attributes = {.min_value = 7.0, .max_value = 3.0},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {4, 2}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the min value is NAN.
    ClampTester{.input = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {2, 3, 4}},
                .attributes = {.min_value = NAN, .max_value = 3.0},
                .output = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {2, 3, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the max value is NAN.
    ClampTester{.input = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {2, 3, 4}},
                .attributes = {.min_value = 0.0, .max_value = NAN},
                .output = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {2, 3, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    ClampTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 2}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    ClampTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
}

struct HardSigmoidTester {
  OperandInfo input;
  absl::optional<float> alpha;
  absl::optional<float> beta;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildHardSigmoid(input_operand_id, output_operand_id, alpha, beta);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, HardSigmoidTest) {
  {
    // Test hardSigmoid operator with default alpha and beta values.
    HardSigmoidTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {3, 4}},
                      .output = {.type = mojom::Operand::DataType::kFloat32,
                                 .dimensions = {3, 4}},
                      .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the alpha value is NAN.
    HardSigmoidTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4}},
                      .alpha = NAN,
                      .beta = 0.5,
                      .output = {.type = mojom::Operand::DataType::kFloat32,
                                 .dimensions = {2, 3, 4}},
                      .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the beta value is NAN.
    HardSigmoidTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                                .dimensions = {2, 3, 4}},
                      .alpha = 1.0,
                      .beta = NAN,
                      .output = {.type = mojom::Operand::DataType::kFloat16,
                                 .dimensions = {2, 3, 4}},
                      .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    HardSigmoidTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {4, 2}},
                      .output = {.type = mojom::Operand::DataType::kFloat32,
                                 .dimensions = {2}},
                      .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    HardSigmoidTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
}

struct Activation {
  mojom::Activation::Tag kind;
  absl::optional<ClampTester::ClampAttributes> clamp_attributes;
  absl::optional<float> elu_alpha;
  absl::optional<float> hard_sigmoid_alpha;
  absl::optional<float> hard_sigmoid_beta;
  absl::optional<float> leaky_relu_alpha;
  absl::optional<float> linear_alpha;
  absl::optional<float> linear_beta;
  absl::optional<float> softplus_steepness;
};

struct BatchNormalizationTester {
  OperandInfo input;
  OperandInfo mean;
  OperandInfo variance;
  absl::optional<OperandInfo> scale;
  absl::optional<OperandInfo> bias;
  struct BatchNormalizationAttributes {
    absl::optional<uint64_t> scale_operand_id;
    absl::optional<uint64_t> bias_operand_id;
    uint32_t axis = 1;
    float epsilon = 1e-5;
    absl::optional<Activation> activation;
  };
  BatchNormalizationAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t mean_operand_id =
        builder.BuildInput("mean", mean.dimensions, mean.type);
    uint64_t variance_operand_id =
        builder.BuildInput("variance", variance.dimensions, variance.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);

    if (scale) {
      attributes.scale_operand_id =
          builder.BuildInput("scale", scale->dimensions, scale->type);
    }
    if (bias) {
      attributes.bias_operand_id =
          builder.BuildInput("bias", bias->dimensions, bias->type);
    }
    builder.BuildBatchNormalization(input_operand_id, mean_operand_id,
                                    variance_operand_id, output_operand_id,
                                    std::move(attributes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, BatchNormalizationTest) {
  {
    // Test building batchNormalization with default option.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building batchNormalization with axis = 3.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {3}},
        .attributes = {.axis = 3},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building batchNormalization with setting optional bias and scale.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with clamp activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kClamp,
                               .clamp_attributes =
                                   ClampTester::ClampAttributes{
                                       .min_value = 1.0, .max_value = 6.0}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with elu activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kElu,
                                      .elu_alpha = 1.0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with hard_sigmoid activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kHardSigmoid,
                               .hard_sigmoid_alpha = 0.2,
                               .hard_sigmoid_beta = 0.5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with leaky relu activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kLeakyRelu,
                               .leaky_relu_alpha = 0.01}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with linear activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kLinear,
                                      .linear_alpha = 0.01,
                                      .linear_beta = 1}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with relu activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kRelu}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test BatchNormalization with sigmoid activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSigmoid}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test BatchNormalization with softmax activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSoftmax}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test BatchNormalization with softplus activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kSoftplus,
                                      .softplus_steepness = 1.0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with softsign activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSoftsign}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test batchNormalization with tanh activation.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kTanh}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when elu activation has alpha < 0.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kElu,
                                      .elu_alpha = -1.0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when input data type and mean data
    // type mismatched.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when the size of mean is not equal to
    // the size of the input dimension denoted by axis.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when input data type and variance data
    // type mismatched.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when the size of variance is not equal
    // to the size of the input dimension denoted by axis.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {1}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when input data is not floating point
    // type.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kInt32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kInt32,
                     .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when axis is out of range [0, N-1].
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {3}},
        .attributes = {.axis = 4},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test batchNormalization when input data type and scale data type
    // mismatched.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when the size of scale is not equal
    // to the size of the input dimension denoted by axis.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test batchNormalization when input data type and bias data type
    // mismatched.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building batchNormalization when the size of bias is not equal
    // to the size of the input dimension denoted by axis.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output type is not the same as input type.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3}},
        .output = {.type = mojom::Operand::DataType::kInt32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output shape is not the same as input shape.
    BatchNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .mean = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .variance = {.type = mojom::Operand::DataType::kFloat32,
                     .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t mean_operand_id =
        builder.BuildInput("mean", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t variance_operand_id =
        builder.BuildInput("variance", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildBatchNormalization(
        input_operand_id, mean_operand_id, variance_operand_id,
        input_operand_id,
        BatchNormalizationTester::BatchNormalizationAttributes{});
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for mean operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t mean_operand_id =
        builder.BuildInput("mean", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t variance_operand_id =
        builder.BuildInput("variance", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildBatchNormalization(
        input_operand_id, mean_operand_id, variance_operand_id, mean_operand_id,
        BatchNormalizationTester::BatchNormalizationAttributes{});
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for variance operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t mean_operand_id =
        builder.BuildInput("mean", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t variance_operand_id =
        builder.BuildInput("variance", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildBatchNormalization(
        input_operand_id, mean_operand_id, variance_operand_id,
        variance_operand_id,
        BatchNormalizationTester::BatchNormalizationAttributes{});
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ConcatTester {
  std::vector<OperandInfo> inputs;
  uint32_t axis;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    std::vector<uint64_t> input_operand_ids;
    input_operand_ids.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
      input_operand_ids.push_back(
          builder.BuildInput(base::StringPrintf("input%zu", i),
                             inputs[i].dimensions, inputs[i].type));
    }
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildConcat(std::move(input_operand_ids), output_operand_id, axis);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ConcatTest) {
  {
    // Test concat operator with three inputs.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 2, 5, 6}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 3, 5, 6}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 6, 5, 6}},
                 .expected = true}
        .Test();
  }
  {
    // Test concat operator when the input is the same as output.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 1, 5, 6}},
                 .expected = true}
        .Test();
  }
  {
    // Test concat operator with empty inputs.
    ConcatTester{
        .inputs = {},
        .axis = 0,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {1}},
        .expected = false}
        .Test();
  }
  {
    // Test concat operator when the inputs' datatypes don't match each
    // other.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}},
                            {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {3, 2, 5, 6}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 3, 5, 6}},
                 .expected = false}
        .Test();
  }
  {
    // Test concat operator when the inputs can not be concatenated.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 2, 5, 6}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 3, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test concat operator when the axis is equal to or greater than the
    // size of dimension.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}}},
                 .axis = 4,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 1, 5, 12}},
                 .expected = false}
        .Test();
  }
  {
    // Test concat operator when the inputs have other axes with different
    // sizes except on the axis.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 1}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 2, 5, 7}},
                 .expected = false}
        .Test();
  }
  {
    // Test concat operator when the concatenated dimension size overflows.
    ConcatTester{
        .inputs = {{.type = mojom::Operand::DataType::kFloat32,
                    .dimensions = {std::numeric_limits<uint32_t>::max()}},
                   {.type = mojom::Operand::DataType::kFloat32,
                    .dimensions = {1}}},
        .axis = 0,
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {0}},
        .expected = false}
        .Test();
  }
  {
    // Test concat operator when the output datatype doesn't match the
    // inputs' datatypes.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 5, 6}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 2, 5, 6}}},
                 .axis = 1,
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {3, 3, 5, 6}},
                 .expected = false}
        .Test();
  }
  {
    // Test concat operator when the output dimension is incorrect.
    ConcatTester{.inputs = {{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3, 1, 2}},
                            {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {1, 1, 2}}},
                 .axis = 0,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {5, 1, 2}},
                 .expected = false}
        .Test();
  }
}

struct Conv2dTester {
  mojom::Conv2d_Type type;
  OperandInfo input;
  OperandInfo filter;
  struct Conv2dAttributes {
    std::vector<uint32_t> padding = {0, 0, 0, 0};
    std::vector<uint32_t> strides = {1, 1};
    std::vector<uint32_t> dilations = {1, 1};
    uint32_t groups = 1;
    mojom::InputOperandLayout input_layout =
        mojom::InputOperandLayout::kChannelsFirst;
    absl::optional<OperandInfo> bias;
    absl::optional<Activation> activation;
  };
  Conv2dAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t filter_operand_id =
        builder.BuildInput("filter", filter.dimensions, filter.type);

    absl::optional<uint64_t> bias_operand_id;
    if (attributes.bias) {
      bias_operand_id = builder.BuildInput("bias", attributes.bias->dimensions,
                                           attributes.bias->type);
    }

    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildConv2d(type, input_operand_id, filter_operand_id,
                        output_operand_id, std::move(attributes),
                        bias_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, Conv2dTest) {
  {
    // Test conv2d with default attributes.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = true}
        .Test();
  }
  {
    // Test conv2d for same upper or lower padding.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.padding = {1, 1, 1, 1}},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test conv2d with strides=2 and padding=1.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.padding = {1, 1, 1, 1}, .strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = true}
        .Test();
  }
  {
    // Test depthwise conv2d by setting groups to input channels.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 4, 2, 2}},
                 .filter = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {4, 1, 2, 2}},
                 .attributes = {.groups = 4},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 4, 1, 1}},
                 .expected = true}
        .Test();
  }
  {
    // Test conv2d with inputLayout="nchw" and filterLayout="oihw".
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 2, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 2, 3, 3}},
                 .attributes = {.input_layout =
                                    mojom::InputOperandLayout::kChannelsFirst},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = true}
        .Test();
  }
  {
    // Test conv2d with clamp activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kClamp,
                               .clamp_attributes =
                                   ClampTester::ClampAttributes{
                                       .min_value = 1.0, .max_value = 6.0}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with elu activation.
    Conv2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kElu,
                                      .elu_alpha = 1.0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with hardSigmoid activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kHardSigmoid,
                               .hard_sigmoid_alpha = 0.2,
                               .hard_sigmoid_beta = 0.5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with leaky relu activation.
    Conv2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kLeakyRelu,
                               .leaky_relu_alpha = 0.01}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with linear activation.
    Conv2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kLinear,
                                      .linear_alpha = 0.01,
                                      .linear_beta = 1}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with relu activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kRelu}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with sigmoid activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSigmoid}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with softmax activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSoftmax}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with softplus activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kSoftplus,
                                      .softplus_steepness = 1.5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with softsign activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSoftsign}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test conv2d with tanh activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kTanh}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when elu activation has alpha < 0.
    Conv2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kElu,
                                      .elu_alpha = -1.0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is not a 4-D tensor.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the filter is not a 4-D tensor.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the filter type doesn't match the input
    // type.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the bias type doesn't match input type.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.bias =
                           OperandInfo{.type = mojom::Operand::DataType::kInt32,
                                       .dimensions = {1}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the bias shape is not equal to
    // [output_channels].
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.bias =
                           OperandInfo{
                               .type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the number of filter input channels
    // doesn't match the result of input channels divided by groups
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.groups = 3},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the max value is less than the min value.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kDirect,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 5, 5}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kClamp,
                               .clamp_attributes =
                                   ClampTester::ClampAttributes{
                                       .min_value = 6.0, .max_value = 1.0}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 2, 1, 1}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    Conv2dTester{.type = mojom::Conv2d_Type::kDirect,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 1, 5, 5}, mojom::Operand::DataType::kFloat32);
    uint64_t filter_operand_id = builder.BuildInput(
        "filter", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);

    builder.BuildConv2d(mojom::Conv2d_Type::kDirect, input_operand_id,
                        filter_operand_id, input_operand_id,
                        Conv2dTester::Conv2dAttributes{}, absl::nullopt);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for filter operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 1, 5, 5}, mojom::Operand::DataType::kFloat32);
    uint64_t filter_operand_id = builder.BuildInput(
        "filter", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);

    builder.BuildConv2d(mojom::Conv2d_Type::kDirect, input_operand_id,
                        filter_operand_id, filter_operand_id,
                        Conv2dTester::Conv2dAttributes{}, absl::nullopt);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

TEST_F(WebNNGraphImplTest, ConvTranspose2dTest) {
  {
    // Test convTranspose2d with default attributes.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with input_layout = kChannelsLast.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 3, 1}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.input_layout =
                                    mojom::InputOperandLayout::kChannelsLast},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 5, 5, 1}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with padding = [1, 1, 1, 1].
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.padding = {1, 1, 1, 1}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with strides = [2, 2].
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 2, 3, 3}},
                 .attributes = {.strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 2, 7, 7}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with strides = [2, 2] and padding = [1, 1, 1,
    // 1].
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.padding = {1, 1, 1, 1}, .strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with group = 3.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.groups = 3},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 5, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with clamp activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kClamp,
                               .clamp_attributes =
                                   ClampTester::ClampAttributes{
                                       .min_value = 1.0, .max_value = 6.0}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with relu activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kRelu}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with sigmoid activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSigmoid}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with softmax activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind =
                                          mojom::Activation::Tag::kSoftmax}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with softplus activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kSoftplus,
                                      .softplus_steepness = 1.5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test convTranspose2d with tanh activation.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{.kind = mojom::Activation::Tag::kTanh}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 5, 5}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 3, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input is not a 4-D tensor.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the filter is not a 4-D tensor.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the number of input channels is not equal
    // to the number of filter input channels.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 1, 3, 3}},
                 .attributes = {.groups = 3},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the number of output channels doesn't
    // match the result of filter output channels multiplied by groups
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 3, 3}},
                 .attributes = {.groups = 3},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the filter type doesn't match the input
    // type.
    Conv2dTester{.type = mojom::Conv2d_Type::kTransposed,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 1, 3, 3}},
                 .filter = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 1, 3, 3}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the bias type doesn't match input type.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.bias =
                           OperandInfo{.type = mojom::Operand::DataType::kInt32,
                                       .dimensions = {1}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the bias shape is not equal to
    // [output_channels].
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.bias =
                           OperandInfo{
                               .type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the max value is less than the min value.
    Conv2dTester{
        .type = mojom::Conv2d_Type::kTransposed,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 3, 3}},
        .filter = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .attributes = {.activation =
                           Activation{
                               .kind = mojom::Activation::Tag::kClamp,
                               .clamp_attributes =
                                   ClampTester::ClampAttributes{
                                       .min_value = 6.0, .max_value = 1.0}}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 5, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t filter_operand_id = builder.BuildInput(
        "filter", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);

    builder.BuildConv2d(mojom::Conv2d_Type::kTransposed, input_operand_id,
                        filter_operand_id, input_operand_id,
                        Conv2dTester::Conv2dAttributes{}, absl::nullopt);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for filter operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t filter_operand_id = builder.BuildInput(
        "filter", {1, 1, 3, 3}, mojom::Operand::DataType::kFloat32);

    builder.BuildConv2d(mojom::Conv2d_Type::kTransposed, input_operand_id,
                        filter_operand_id, filter_operand_id,
                        Conv2dTester::Conv2dAttributes{}, absl::nullopt);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ElementWiseBinaryTester {
  mojom::ElementWiseBinary::Kind kind;
  OperandInfo lhs;
  OperandInfo rhs;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t lhs_operand_id =
        builder.BuildInput("lhs", lhs.dimensions, lhs.type);
    uint64_t rhs_operand_id =
        builder.BuildInput("rhs", rhs.dimensions, rhs.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildElementWiseBinary(kind, lhs_operand_id, rhs_operand_id,
                                   output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }

  void TestLogicalOperators() {
    const mojom::ElementWiseBinary::Kind kLogicalOperators[] = {
        mojom::ElementWiseBinary::Kind::kEqual,
        mojom::ElementWiseBinary::Kind::kGreater,
        mojom::ElementWiseBinary::Kind::kGreaterOrEqual,
        mojom::ElementWiseBinary::Kind::kLesser,
        mojom::ElementWiseBinary::Kind::kLesserOrEqual,
    };

    for (const auto& op : kLogicalOperators) {
      kind = op;
      Test();
    }
  }
};

TEST_F(WebNNGraphImplTest, ElementWiseBinaryTest) {
  // Testing building with two input dimensions - {8, 1, 6, 1} and {7, 1, 5}.
  // Both the a and b dimensions have axes with length one that are expanded to
  // a larger size during the broadcast operation.
  // a_dimensions     (4d) 8 * 1 * 6 * 1
  // b_dimensions     (3d)     7 * 1 * 5
  // output_dimenions (4d) 8 * 7 * 6 * 5
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kAdd,
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {8, 1, 6, 1}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {7, 1, 5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {8, 7, 6, 5}},
        .expected = true}
        .Test();
  }

  // Testing building with two input dimensions - {4, 2, 1} and {4}.
  // a_dimensions     (3d) 4 * 2 * 1
  // b_dimensions     (1d)         4
  // output_dimenions (3d) 4 * 2 * 4
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kSub,
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2, 1}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {4, 2, 4}},
        .expected = true}
        .Test();
  }

  // Test the invalid graph for the input shapes are not broadcastable.
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kMul,
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {4, 2}},
        .expected = false}
        .Test();
  }

  // Test the invalid graph for the output shapes are not expected.
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kDiv,
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2}},
        .expected = false}
        .Test();
  }

  // Test the invalid graph for input types don't match.
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kMax,
        .lhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .rhs = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2}},
        .expected = false}
        .Test();
  }

  // Test the invalid graph for output types don't match.
  {
    ElementWiseBinaryTester{
        .kind = mojom::ElementWiseBinary::Kind::kMin,
        .lhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
}

TEST_F(WebNNGraphImplTest, ElementWiseBinaryLogicalTest) {
  // Testing building with two input dimensions - {8, 1, 6, 1} and {7, 1, 5}.
  // Both the a and b dimensions have axes with length one that are expanded to
  // a larger size during the broadcast operation.
  // a_dimensions     (4d) 8 * 1 * 6 * 1
  // b_dimensions     (3d)     7 * 1 * 5
  // output_dimenions (4d) 8 * 7 * 6 * 5
  {
    ElementWiseBinaryTester{.lhs = {.type = mojom::Operand::DataType::kFloat32,
                                    .dimensions = {8, 1, 6, 1}},
                            .rhs = {.type = mojom::Operand::DataType::kFloat32,
                                    .dimensions = {7, 1, 5}},
                            .output = {.type = mojom::Operand::DataType::kUint8,
                                       .dimensions = {8, 7, 6, 5}},
                            .expected = true}
        .TestLogicalOperators();
  }

  // Testing building with two input dimensions - {4, 2, 1} and {4}.
  // a_dimensions     (3d) 4 * 2 * 1
  // b_dimensions     (1d)         4
  // output_dimenions (3d) 4 * 2 * 4
  {
    ElementWiseBinaryTester{
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2, 1}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4}},
        .output = {.type = mojom::Operand::DataType::kUint8,
                   .dimensions = {4, 2, 4}},
        .expected = true}
        .TestLogicalOperators();
  }

  // Test the invalid graph for the input shapes are not broadcastable.
  {
    ElementWiseBinaryTester{
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4}},
        .output = {.type = mojom::Operand::DataType::kUint8,
                   .dimensions = {4, 2}},
        .expected = false}
        .TestLogicalOperators();
  }

  // Test the invalid graph for the output shapes are not expected.
  {
    ElementWiseBinaryTester{
        .lhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32,
                .dimensions = {4, 2}},
        .output = {.type = mojom::Operand::DataType::kUint8, .dimensions = {2}},
        .expected = false}
        .TestLogicalOperators();
  }

  // Test the invalid graph for input types don't match.
  {
    ElementWiseBinaryTester{
        .lhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .rhs = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kUint8, .dimensions = {2}},
        .expected = false}
        .TestLogicalOperators();
  }

  // Test the invalid graph for when the output data type is not kUint8 for
  // logical operators.
  {
    ElementWiseBinaryTester{
        .lhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .rhs = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2}},
        .expected = false}
        .TestLogicalOperators();
  }
}

struct ElementWiseUnaryTester {
  mojom::ElementWiseUnary::Kind kind;
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildElementWiseUnary(kind, input_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

// Test the data type support for element-wise unary operators.
// The data type support is defined in the first parameter of the tuple
// as a std::pair of mojom::ElementWiseUnary::Kind and array of
// datatypes supported by the operator.
class ElementWiseUnaryDataTypeFixture
    : public testing::TestWithParam<
          std::tuple<std::pair<mojom::ElementWiseUnary::Kind,
                               std::vector<mojom::Operand::DataType>>,
                     mojom::Operand::DataType,
                     mojom::Operand::DataType>> {
 public:
  // Populate meaningful test suffixes.
  struct PrintToStringParamName {
    template <class ParamType>
    std::string operator()(
        const testing::TestParamInfo<ParamType>& info) const {
      std::string test_name =
          base::StrCat({OpKindToString(std::get<0>(info.param).first), "_",
                        DataTypeToString(std::get<1>(info.param)), "_",
                        DataTypeToString(std::get<2>(info.param))});
      return test_name;
    }
  };

  void TestDataTypeSupportWithDimensions(
      const std::vector<uint32_t>& dimensions) {
    auto [operator_trait, inputDataType, outputDataType] = GetParam();
    const mojom::ElementWiseUnary::Kind& kind = operator_trait.first;
    // Some operators support dissimilar input and output data types.
    const std::set<mojom::ElementWiseUnary::Kind>
        kOperatorsWithDissimilarDatatypeSupport = {
            mojom::ElementWiseUnary::Kind::kCast};

    // Check if data types match, or if the operator supports mismatch.
    // Check if the data type is supported by the operator.
    const bool expected =
        (inputDataType == outputDataType ||
         kOperatorsWithDissimilarDatatypeSupport.contains(kind)) &&
        base::Contains(operator_trait.second, inputDataType);

    ElementWiseUnaryTester{
        .kind = kind,
        .input = {.type = inputDataType, .dimensions = dimensions},
        .output = {.type = outputDataType, .dimensions = dimensions},
        .expected = expected}
        .Test();
  }
};

TEST_P(ElementWiseUnaryDataTypeFixture, TestUnaryOperandDataTypeSupport) {
  TestDataTypeSupportWithDimensions(std::vector<uint32_t>{1, 2, 3, 1});
}

TEST_P(ElementWiseUnaryDataTypeFixture, TestUnaryOperandScalarDataTypeSupport) {
  TestDataTypeSupportWithDimensions(std::vector<uint32_t>{});
}

INSTANTIATE_TEST_SUITE_P(
    WebNNGraphImplTest,
    ElementWiseUnaryDataTypeFixture,
    ::testing::Combine(
        ::testing::ValuesIn({
            std::make_pair(mojom::ElementWiseUnary::Kind::kLogicalNot,
                           std::vector<mojom::Operand::DataType>{
                               mojom::Operand::DataType::kUint8}),
            std::make_pair(mojom::ElementWiseUnary::Kind::kIdentity,
                           std::vector<mojom::Operand::DataType>(
                               kAllOperandDataTypes,
                               std::end(kAllOperandDataTypes))),
            std::make_pair(mojom::ElementWiseUnary::Kind::kSqrt,
                           std::vector<mojom::Operand::DataType>{
                               mojom::Operand::DataType::kFloat16,
                               mojom::Operand::DataType::kFloat32}),
            std::make_pair(mojom::ElementWiseUnary::Kind::kErf,
                           std::vector<mojom::Operand::DataType>{
                               mojom::Operand::DataType::kFloat16,
                               mojom::Operand::DataType::kFloat32}),
            std::make_pair(mojom::ElementWiseUnary::Kind::kReciprocal,
                           std::vector<mojom::Operand::DataType>{
                               mojom::Operand::DataType::kFloat16,
                               mojom::Operand::DataType::kFloat32}),
            std::make_pair(mojom::ElementWiseUnary::Kind::kCast,
                           std::vector<mojom::Operand::DataType>(
                               kAllOperandDataTypes,
                               std::end(kAllOperandDataTypes))),
        }),
        ::testing::ValuesIn(kAllOperandDataTypes),
        ::testing::ValuesIn(kAllOperandDataTypes)),
    ElementWiseUnaryDataTypeFixture::PrintToStringParamName());

TEST_F(WebNNGraphImplTest, ElementWiseUnaryTest) {
  {
    // Test building element-wise abs.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kAbs,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise ceil.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kCeil,
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise cos.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kCos,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise exp.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kExp,
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise floor.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kFloor,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise log.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kLog,
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise neg.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kNeg,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise sin.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kSin,
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building element-wise tan.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kTan,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 4, 5}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 4, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid element-wise abs graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kAbs,
                           .input = {.type = mojom::Operand::DataType::kUint32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise neg graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kNeg,
                           .input = {.type = mojom::Operand::DataType::kUint8,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint8,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise ceil graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kCeil,
                           .input = {.type = mojom::Operand::DataType::kUint32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise cos graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kCos,
                           .input = {.type = mojom::Operand::DataType::kUint32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise exp graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kExp,
                           .input = {.type = mojom::Operand::DataType::kUint8,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint8,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise floor graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kFloor,
                           .input = {.type = mojom::Operand::DataType::kInt8,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kInt8,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise log graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kLog,
                           .input = {.type = mojom::Operand::DataType::kInt32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kInt32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise sin graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kSin,
                           .input = {.type = mojom::Operand::DataType::kUint32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid element-wise tan graph for the input with
    // unsupported data type.
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kTan,
                           .input = {.type = mojom::Operand::DataType::kUint32,
                                     .dimensions = {1, 2, 3, 4}},
                           .output = {.type = mojom::Operand::DataType::kUint32,
                                      .dimensions = {1, 2, 3, 4}},
                           .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input and output shapes don't match.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kAbs,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 4, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output type don't match.
    ElementWiseUnaryTester{
        .kind = mojom::ElementWiseUnary::Kind::kCeil,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 4}},
        .expected = false}
        .Test();
  }
  // Test case for cast where dimensions don't match
  {
    ElementWiseUnaryTester{.kind = mojom::ElementWiseUnary::Kind::kCast,
                           .input = {.type = mojom::Operand::DataType::kUint8,
                                     .dimensions = {1, 2, 3, 1}},
                           .output = {.type = mojom::Operand::DataType::kInt8,
                                      .dimensions = {1, 2, 3, 2}},
                           .expected = false}
        .Test();
  }
}

struct EluTester {
  OperandInfo input;
  OperandInfo output;
  float alpha = 1.0;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildElu(input_operand_id, output_operand_id, alpha);

    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, EluTest) {
  {
    // Test elu operator for 2-D tensor with float32 input.
    EluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 6}},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {2, 6}},
              .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the alpha is less than or equal to 0.
    EluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2}},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {2}},
              .alpha = 0,
              .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the alpha is NAN.
    EluTester{
        .input = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .alpha = NAN,
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not as expected.
    EluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {4, 2}},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {2}},
              .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output data types which don't match.
    EluTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input data type is not floating
    // point.
    EluTester{
        .input = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildElu(input_operand_id, input_operand_id, /*alpha*/ 1.0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ExpandTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildExpand(input_operand_id, output_operand_id);

    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ExpandTest) {
  {
    // Test building expand with the output shapes that are the same as
    // input.
    ExpandTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 6}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 6}},
                 .expected = true}
        .Test();
  }
  {
    // Test building expand with the output shapes that are broadcastable.
    ExpandTester{.input = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {3, 1, 5}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {3, 4, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test building expand with the output shapes that are broadcastable
    // and the number of output shapes larger than input.
    ExpandTester{.input = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {2, 5}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {3, 2, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the input shapes are not the same as
    // output shape and not broadcastable.
    ExpandTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 6, 2}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {4, 3, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input shapes are not broadcastable.
    ExpandTester{
        .input = {.type = mojom::Operand::DataType::kInt32, .dimensions = {5}},
        .output = {.type = mojom::Operand::DataType::kInt32,
                   .dimensions = {5, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output data types which don't match.
    ExpandTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildExpand(input_operand_id, input_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct GatherTester {
  OperandInfo input;
  struct GatherAttributes {
    OperandInfo indices;
    uint32_t axis;
  };
  GatherAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t indices_operand_id = builder.BuildInput(
        "indices", attributes.indices.dimensions, attributes.indices.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildGather(input_operand_id, indices_operand_id, output_operand_id,
                        attributes.axis);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, GatherTest) {
  {
    // Test gather operator with 3-D input and 2-D indices.
    GatherTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {3, 4, 5}},
        .attributes = {.indices = {.type = mojom::Operand::DataType::kUint32,
                                   .dimensions = {6, 7}},
                       .axis = 1},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 6, 7, 5}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for the axis is too large.
    GatherTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {3, 4, 5}},
        .attributes = {.indices = {.type = mojom::Operand::DataType::kUint32,
                                   .dimensions = {6, 7}},
                       .axis = 3},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {3, 4, 5, 6, 7}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the indices data type is floating point.
    GatherTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {3, 4, 5}},
        .attributes = {.indices = {.type = mojom::Operand::DataType::kFloat16,
                                   .dimensions = {6, 7}},
                       .axis = 1},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 6, 7, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    GatherTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {3, 4, 5}},
        .attributes = {.indices = {.type = mojom::Operand::DataType::kUint32,
                                   .dimensions = {6, 7}},
                       .axis = 1},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4, 6, 7, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    GatherTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {3, 4, 5}},
        .attributes = {.indices = {.type = mojom::Operand::DataType::kUint32,
                                   .dimensions = {6, 7}},
                       .axis = 1},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {3, 6, 7, 5}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the output is as same as the input.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t indices_operand_id =
        builder.BuildInput("indices", {2}, mojom::Operand::DataType::kUint32);
    builder.BuildGather(input_operand_id, indices_operand_id, input_operand_id,
                        /*axis*/ 0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the output is as same as the indices.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {3}, mojom::Operand::DataType::kUint32);
    uint64_t indices_operand_id =
        builder.BuildInput("indices", {3}, mojom::Operand::DataType::kUint32);
    builder.BuildGather(input_operand_id, indices_operand_id,
                        indices_operand_id, /*axis*/ 0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct GemmTester {
  OperandInfo a;
  OperandInfo b;
  absl::optional<OperandInfo> c;
  struct GemmAttributes {
    absl::optional<uint64_t> c_operand_id;
    float alpha = 1.0;
    float beta = 1.0;
    bool a_transpose = false;
    bool b_transpose = false;
  };
  GemmAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t a_operand_id = builder.BuildInput("a", a.dimensions, a.type);
    uint64_t b_operand_id = builder.BuildInput("b", b.dimensions, b.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);

    if (c) {
      attributes.c_operand_id = builder.BuildInput("c", c->dimensions, c->type);
    }
    builder.BuildGemm(a_operand_id, b_operand_id, output_operand_id,
                      std::move(attributes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, GemmTest) {
  {
    // Test building gemm with default option.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building gemm with aTranspose = true.
    // Transposed a_dimensions would be {3, 2} and it's compatible with
    // b_dimensions {2, 4}.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 4}},
        .attributes = {.a_transpose = true},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building gemm with bTranspose = true.
    // Transposed b_dimensions would be {3, 4} and it's compatible with
    // a_dimensions {2, 3}.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4, 3}},
        .attributes = {.b_transpose = true},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building gemm with setting optional input C.
    // The output dimensions of a * b would be {2, 4} and c_dimensions {4}
    // is able to broadcast to {2, 4}.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .c = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building gemm with two matrices - {2, 3} and {2, 4} that can't
    // be multiplied together due to incompatible dimensions.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test building gemm with aTranspose = true, bTranspose = true.
    // The output dimensions of a * b would be {2, 4} and c_dimension {2, 3}
    // is incompatible with {2, 4}.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .c = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test building gemm with aTranspose = true, bTranspose = true.
    // Set optional input C with type = int32 and it mismatches with input
    // type float32.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 2}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {4, 3}},
        .c = OperandInfo{.type = mojom::Operand::DataType::kInt32,
                         .dimensions = {2, 4}},
        .attributes = {.a_transpose = true, .b_transpose = true},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    GemmTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = false}
        .Test();
  }
}

struct InstanceNormalizationTester {
  OperandInfo input;
  absl::optional<OperandInfo> scale;
  absl::optional<OperandInfo> bias;
  struct InstanceNormalizationAttributes {
    absl::optional<uint64_t> scale_operand_id;
    absl::optional<uint64_t> bias_operand_id;
    mojom::InputOperandLayout layout =
        mojom::InputOperandLayout::kChannelsFirst;
    float epsilon = 1e-5;
  };
  InstanceNormalizationAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);

    if (scale) {
      attributes.scale_operand_id =
          builder.BuildInput("scale", scale->dimensions, scale->type);
    }
    if (bias) {
      attributes.bias_operand_id =
          builder.BuildInput("bias", bias->dimensions, bias->type);
    }
    builder.BuildInstanceNormalization(input_operand_id, output_operand_id,
                                       std::move(attributes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, InstanceNormalizationTest) {
  {
    // Test building instanceNormalization with default option.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building instanceNormalization with layout = kChannelsLast.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3}},
        .attributes = {.layout = mojom::InputOperandLayout::kChannelsLast},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test building instanceNormalization with default layout = kChannelsFirst.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = true}
        .Test();
  }
  {
    // Test instanceNormalization when input data type and scale data type
    // mismatched.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building instanceNormalization when the size of scale is not equal
    // to the size of the feature dimension of the input.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test instanceNormalization when input data type and bias data type
    // mismatched.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test building instanceNormalization when the size of bias is not equal
    // to the size of the feature dimension of the input.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2}},
        .attributes = {.layout = mojom::InputOperandLayout::kChannelsLast},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output type is not the same as input type.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .output = {.type = mojom::Operand::DataType::kInt32,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output shape is not the same as input shape.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input is not a 4-D tensor.
    InstanceNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildInstanceNormalization(
        input_operand_id, input_operand_id,
        InstanceNormalizationTester::InstanceNormalizationAttributes{});
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the output is the same as the scale.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t scale_operand_id =
        builder.BuildInput("scale", {2}, mojom::Operand::DataType::kFloat32);

    InstanceNormalizationTester::InstanceNormalizationAttributes attributes;
    attributes.scale_operand_id = scale_operand_id;

    builder.BuildInstanceNormalization(input_operand_id, scale_operand_id,
                                       std::move(attributes));
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the output is the same as the bias.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t bias_operand_id =
        builder.BuildInput("bias", {2}, mojom::Operand::DataType::kFloat32);

    InstanceNormalizationTester::InstanceNormalizationAttributes attributes;
    attributes.bias_operand_id = bias_operand_id;

    builder.BuildInstanceNormalization(input_operand_id, bias_operand_id,
                                       std::move(attributes));
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct LayerNormalizationTester {
  OperandInfo input;
  absl::optional<OperandInfo> scale;
  absl::optional<OperandInfo> bias;
  struct LayerNormalizationAttributes {
    absl::optional<uint64_t> scale_operand_id;
    absl::optional<uint64_t> bias_operand_id;
    std::vector<uint32_t> axes;
    float epsilon = 1e-5;
  };
  LayerNormalizationAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);

    if (scale.has_value()) {
      attributes.scale_operand_id =
          builder.BuildInput("scale", scale->dimensions, scale->type);
    }
    if (bias.has_value()) {
      attributes.bias_operand_id =
          builder.BuildInput("bias", bias->dimensions, bias->type);
    }
    builder.BuildLayerNormalization(input_operand_id, output_operand_id,
                                    std::move(attributes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, LayerNormalizationTest) {
  {
    // Test building layerNormalization with default option for scalar input.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {}},
        .attributes = {.axes = {}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {}},
        .expected = true}
        .Test();
  }
  {
    // Test building layerNormalization with 4-D input.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {3, 4}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat16,
                            .dimensions = {3, 4}},
        .attributes = {.axes = {2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the input is a scalar and the axes is not
    // empty.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {}},
        .attributes = {.axes = {0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input data type is int64.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kInt64, .dimensions = {1}},
        .attributes = {.axes = {}},
        .output = {.type = mojom::Operand::DataType::kInt64, .dimensions = {1}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the axes have duplications.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2}},
        .attributes = {.axes = {0, 0}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the axis is greater than the input rank.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2}},
        .attributes = {.axes = {2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the bias type doesn't match the input type.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .bias = OperandInfo{.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 4}},
        .attributes = {.axes = {2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the scale shape doesn't match the reduction
    // dimensions.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .scale = OperandInfo{.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {2, 3}},
        .attributes = {.axes = {2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .attributes = {.axes = {}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {1, 2, 3, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output type doesn't match the input type.
    LayerNormalizationTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {1, 2, 3, 4}},
        .attributes = {.axes = {}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the output is the same as the input.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildLayerNormalization(
        input_operand_id, input_operand_id,
        LayerNormalizationTester::LayerNormalizationAttributes{});
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the output is the same as the scale.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t scale_operand_id = builder.BuildInput(
        "scale", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);

    LayerNormalizationTester::LayerNormalizationAttributes attributes;
    attributes.scale_operand_id = scale_operand_id;
    attributes.axes = {0, 1, 2, 3};

    builder.BuildLayerNormalization(input_operand_id, scale_operand_id,
                                    std::move(attributes));
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the output is the same as the bias.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t bias_operand_id = builder.BuildInput(
        "bias", {1, 2, 3, 4}, mojom::Operand::DataType::kFloat32);

    LayerNormalizationTester::LayerNormalizationAttributes attributes;
    attributes.bias_operand_id = bias_operand_id;
    attributes.axes = {0, 1, 2, 3};

    builder.BuildLayerNormalization(input_operand_id, bias_operand_id,
                                    std::move(attributes));
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct MatmulTester {
  OperandInfo a;
  OperandInfo b;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t a_operand_id = builder.BuildInput("a", a.dimensions, a.type);
    uint64_t b_operand_id = builder.BuildInput("b", b.dimensions, b.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);

    builder.BuildMatmul(a_operand_id, b_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, MatmulTest) {
  {
    // Test building matmul with 2-D * 2-D.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building matmul with 2-D * 4-D.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32,
              .dimensions = {2, 3, 3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 3, 2, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test building matmul with 3-D * 4-D using broadcasting.
    MatmulTester{.a = {.type = mojom::Operand::DataType::kFloat32,
                       .dimensions = {2, 2, 3}},
                 .b = {.type = mojom::Operand::DataType::kFloat32,
                       .dimensions = {3, 1, 3, 4}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 2, 2, 4}},
                 .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for one input rank is smaller than 2.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the number of columns in first matrix
    // mismatches with the number of rows in second matrix.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 2}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input shapes are not broadcastable.
    MatmulTester{.a = {.type = mojom::Operand::DataType::kFloat32,
                       .dimensions = {3, 2, 3}},
                 .b = {.type = mojom::Operand::DataType::kFloat32,
                       .dimensions = {2, 3, 4}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 4}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {3, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input types are not same.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output type is not the same as input type.
    MatmulTester{
        .a = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {2, 3}},
        .b = {.type = mojom::Operand::DataType::kFloat32, .dimensions = {3, 4}},
        .output = {.type = mojom::Operand::DataType::kInt32,
                   .dimensions = {2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the output is as same as one input.
    GraphInfoBuilder builder;
    uint64_t a_operand_id =
        builder.BuildInput("a", {2, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t b_operand_id =
        builder.BuildInput("b", {3, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildMatmul(a_operand_id, b_operand_id, a_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct PadTester {
  OperandInfo input;
  std::vector<uint32_t> beginning_padding;
  std::vector<uint32_t> ending_padding;
  mojom::PaddingMode::Tag mode = mojom::PaddingMode::Tag::kConstant;
  float value = 0;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildPad(input_operand_id, output_operand_id, beginning_padding,
                     ending_padding, mode, value);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, PadTest) {
  {
    // Test pad with default options, beginningPadding = {1, 2} and
    // endingPadding = {1, 2}.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {1, 2},
              .ending_padding = {1, 2},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 7}},
              .expected = true}
        .Test();
  }
  {
    // Test pad with mode = "edge", beginningPadding = {1, 2} and
    // endingPadding = {1, 2}.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {1, 2},
              .ending_padding = {1, 2},
              .mode = mojom::PaddingMode::Tag::kEdge,
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 7}},
              .expected = true}
        .Test();
  }
  {
    // Test pad with value = 1, beginningPadding = {1, 2} and
    // endingPadding = {1, 2}.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {1, 2},
              .ending_padding = {1, 2},
              .value = 1,
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 7}},
              .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the length of beginningPadding is not
    // equal to the input rank.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {1},
              .ending_padding = {1, 2},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 7}},
              .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the length of endingPadding is not equal
    // to the input rank.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {1, 0},
              .ending_padding = {1, 2, 0},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 7}},
              .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the padding of one dimension is too
    // large.
    PadTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                        .dimensions = {2, 3}},
              .beginning_padding = {2294967295, 0},
              .ending_padding = {3294967295, 2},
              .output = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {1294967294, 5}},
              .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2, 3}, mojom::Operand::DataType::kFloat32);
    builder.BuildPad(input_operand_id, input_operand_id, {1, 1}, {1, 1},
                     mojom::PaddingMode::Tag::kConstant, 0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct Pool2dTester {
  OperandInfo input;
  struct Pool2dAttributes {
    std::vector<uint32_t> window_dimensions;
    std::vector<uint32_t> padding = {0, 0, 0, 0};
    std::vector<uint32_t> strides = {1, 1};
    std::vector<uint32_t> dilations = {1, 1};
    mojom::InputOperandLayout layout;
  };
  Pool2dAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    Test(mojom::Pool2d::Kind::kAveragePool2d);
    Test(mojom::Pool2d::Kind::kMaxPool2d);
  }

  void Test(mojom::Pool2d::Kind kind) {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildPool2d(kind, input_operand_id, output_operand_id,
                        std::move(attributes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, Pool2dTest) {
  {
    // Test pool2d with default attributes.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {1, 1}, .strides = {1, 1}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 4, 4}},
                 .expected = true}
        .Test();
  }
  {
    // Test pool2d with window dimensions.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 3, 5, 5}},
                 .attributes = {.window_dimensions = {2, 2}, .strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 3, 3, 3}},
                 .expected = true}
        .Test();
  }
  {
    // Test pool2d with strides=2, padding=1 and floor rounding.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 3, 7, 7}},
                 .attributes = {.window_dimensions = {4, 4},
                                .padding = {1, 1, 1, 1},
                                .strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 3, 3, 3}},
                 .expected = true}
        .Test();
  }
  {
    // Test pool2d with strides=2, padding=1 and ceil rounding.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kInt8,
                           .dimensions = {1, 3, 7, 7}},
                 .attributes = {.window_dimensions = {4, 4},
                                .padding = {1, 1, 1, 1},
                                .strides = {2, 2}},
                 .output = {.type = mojom::Operand::DataType::kInt8,
                            .dimensions = {1, 3, 4, 4}},
                 .expected = true}
        .Test();
  }
  {
    // Test pool2d with layout="nhwc".
    Pool2dTester{
        .input = {.type = mojom::Operand::DataType::kInt8,
                  .dimensions = {1, 5, 5, 2}},
        .attributes = {.window_dimensions = {3, 3},
                       .strides = {1, 1},
                       .layout = mojom::InputOperandLayout::kChannelsLast},
        .output = {.type = mojom::Operand::DataType::kInt8,
                   .dimensions = {1, 3, 3, 2}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the input is not a 4-D tensor.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 5, 5}},
                 .attributes = {.window_dimensions = {5, 5},
                                .padding = {2, 2, 2, 2},
                                .strides = {1, 1}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {3, 5, 5}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when window dimensions are 0.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {0, 0}, .strides = {1, 1}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 4, 4}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when strides are 0.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {1, 1}, .strides = {0, 0}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 4, 4}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when dilations are 0.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {1, 1},
                                .strides = {1, 1},
                                .dilations = {0, 0}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 4, 4}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {4, 4}, .strides = {1, 1}},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 2, 1, 1}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    Pool2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1, 3, 4, 4}},
                 .attributes = {.window_dimensions = {4, 4}, .strides = {1, 1}},
                 .output = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 3, 1, 1}},
                 .expected = false}
        .Test();
  }
}

struct PreluTester {
  OperandInfo input;
  OperandInfo slope;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t slope_operand_id =
        builder.BuildInput("slope", slope.dimensions, slope.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildPrelu(input_operand_id, slope_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, PreluTest) {
  {
    // Test prelu operator when the input and the slope have the same shape.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 2, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 2, 5}},
                .expected = true}
        .Test();
  }
  {
    // Test prelu operator with a broadcastable slope.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 1, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 2, 5}},
                .expected = true}
        .Test();
  }
  {
    // Test the invalid graph with an invalid slope.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 2, 5}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the datatype isn't floating point.
    PreluTester{.input = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {3, 2, 5}},
                .output = {.type = mojom::Operand::DataType::kInt32,
                           .dimensions = {3, 2, 5}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the slope datatype doesn't match the
    // input's datatype.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {3, 2, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat16,
                           .dimensions = {3, 2, 5}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the output datatype doesn't match the
    // input's datatype.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {3, 2, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {3, 2, 5}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    PreluTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {3, 2, 5}},
                .slope = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {3, 2, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat16,
                           .dimensions = {3, 2, 6}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t slope_operand_id =
        builder.BuildInput("slope", {2, 3}, mojom::Operand::DataType::kFloat32);
    builder.BuildPrelu(input_operand_id, slope_operand_id, input_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the slope is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2, 3}, mojom::Operand::DataType::kFloat32);
    uint64_t output_operand_id = builder.BuildOutput(
        "output", {2, 3}, mojom::Operand::DataType::kFloat32);
    builder.BuildPrelu(input_operand_id, output_operand_id, output_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ReduceTester {
  mojom::Reduce::Kind kind;
  OperandInfo input;
  std::vector<uint32_t> axes;
  bool keep_dimensions = false;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildReduce(kind, input_operand_id, output_operand_id, axes,
                        keep_dimensions);

    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ReduceTest) {
  {
    // Test reduce operator with axes = {0, 2} and keep_dimensions = true.
    ReduceTester{.kind = mojom::Reduce::Kind::kL1,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4, 5}},
                 .axes = {0, 2},
                 .keep_dimensions = true,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3, 1, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test reduce operator with axes = {2} and keep_dimensions = false.
    ReduceTester{.kind = mojom::Reduce::Kind::kL2,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4, 5}},
                 .axes = {2},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3, 5}},
                 .expected = true}
        .Test();
  }
  {
    ReduceTester{.kind = mojom::Reduce::Kind::kMin,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4, 5}},
                 .axes = {0, 1, 2, 3},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {}},
                 .expected = true}
        .Test();
  }
  {
    // Test reduce operator with empty axes = {}.
    ReduceTester{.kind = mojom::Reduce::Kind::kMin,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4, 5}},
                 .axes = {},
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3, 4, 5}},
                 .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the rank of axes is larger than the
    // input rank.
    ReduceTester{.kind = mojom::Reduce::Kind::kMax,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3}},
                 .axes = {0, 1, 2},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the axes contains duplicate values.
    ReduceTester{.kind = mojom::Reduce::Kind::kMean,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3}},
                 .axes = {1, 1},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when one value in axes is greater than
    // input_rank - 1.
    ReduceTester{.kind = mojom::Reduce::Kind::kSum,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3}},
                 .axes = {2},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output shapes are not expected.
    ReduceTester{.kind = mojom::Reduce::Kind::kProduct,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3}},
                 .axes = {0},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    ReduceTester{.kind = mojom::Reduce::Kind::kLogSum,
                 .input = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3}},
                 .axes = {0},
                 .keep_dimensions = false,
                 .output = {.type = mojom::Operand::DataType::kFloat16,
                            .dimensions = {3}},
                 .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input type is not one of float types
    // for reduceLogSum.
    ReduceTester{
        .kind = mojom::Reduce::Kind::kLogSum,
        .input = {.type = mojom::Operand::DataType::kInt32,
                  .dimensions = {2, 3}},
        .axes = {0},
        .keep_dimensions = false,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input type is not one of float types
    // for reduceLogSumExp.
    ReduceTester{
        .kind = mojom::Reduce::Kind::kLogSumExp,
        .input = {.type = mojom::Operand::DataType::kInt32,
                  .dimensions = {2, 3}},
        .axes = {0},
        .keep_dimensions = false,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input type is not one of float types
    // for reduceL2.
    ReduceTester{
        .kind = mojom::Reduce::Kind::kL2,
        .input = {.type = mojom::Operand::DataType::kInt32,
                  .dimensions = {2, 3}},
        .axes = {0},
        .keep_dimensions = false,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input type is not one of float types
    // for reduceMean.
    ReduceTester{
        .kind = mojom::Reduce::Kind::kMean,
        .input = {.type = mojom::Operand::DataType::kInt32,
                  .dimensions = {2, 3}},
        .axes = {0},
        .keep_dimensions = false,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the input type and the output type are not
    // same.
    ReduceTester{
        .kind = mojom::Reduce::Kind::kLogSum,
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2, 3}},
        .axes = {0},
        .keep_dimensions = false,
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2, 3}, mojom::Operand::DataType::kFloat32);
    builder.BuildReduce(mojom::Reduce::Kind::kSumSquare, input_operand_id,
                        input_operand_id, {0}, false);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ReluTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildRelu(input_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ReluTest) {
  {
    // Test relu operator for 3-D tensor with float32 input.
    ReluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {2, 6, 4}},
               .output = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {2, 6, 4}},
               .expected = true}
        .Test();
  }
  {
    // Test relu operator for 4-D tensor with int32 input.
    ReluTester{.input = {.type = mojom::Operand::DataType::kInt32,
                         .dimensions = {1, 5, 3, 7}},
               .output = {.type = mojom::Operand::DataType::kInt32,
                          .dimensions = {1, 5, 3, 7}},
               .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    ReluTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                         .dimensions = {4, 2}},
               .output = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {2}},
               .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    ReluTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
}

struct Resample2dTester {
  OperandInfo input;
  struct Resample2dAttributes {
    mojom::Resample2d::InterpolationMode mode =
        mojom::Resample2d::InterpolationMode::kNearestNeighbor;
    absl::optional<std::vector<float>> scales;
    std::vector<uint32_t> axes = {2, 3};
  };
  Resample2dAttributes attributes;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildResample2d(input_operand_id, output_operand_id, attributes);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, Resample2dTest) {
  {
    // Test resample2d with "NearestNeighbor" mode and axes = [2, 3].
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 1, 2, 4}},
                     .expected = true}
        .Test();
  }
  {
    // Test resample2d with "Linear" mode, axes = [1, 2] and explicit scales
    // = [2, 2].
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 4, 1}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{2, 2},
                       .axes = {1, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 4, 8, 1}},
        .expected = true}
        .Test();
  }
  {
    // Test resample2d with "Linear" mode, axes = [1, 2] and explicit scales
    // = [2, 2.2] which is not exactly output dimensions / input dimensions.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 4, 1}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{2, 2.2},
                       .axes = {1, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 4, 8, 1}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .output = {.type = mojom::Operand::DataType::kFloat16,
                                .dimensions = {1, 1, 4, 8}},
                     .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input is not a 4-D tensor.
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 1, 2, 4}},
                     .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output is not a 4-D tensor.
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 1, 2}},
                     .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output dimensions that don't match the
    // calculated dimensions by scales.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 2, 4, 1}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{2, 2},
                       .axes = {1, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 5, 8, 1}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the scale height is too large.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 34902, 23243}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{232433, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the scale height is too small.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 2, 4}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{0.02, 0.8}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 0, 3}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the scale width is too large.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 34902, 23243}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{20, 434324}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 2, 4}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the scale width is too small.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 2, 4}},
        .attributes = {.mode = mojom::Resample2d::InterpolationMode::kLinear,
                       .scales = std::vector<float>{0.7, 0.1}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 1, 1, 0}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the scales are negative.
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .attributes{.scales = std::vector<float>{1.0, -2.0}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 2, 4, 4}},
                     .expected = false}
        .Test();
  }
  // Test the invalid graph when the dimensions of the input tensor to which
  // the interpolation algorithm applies are not two consecutive dimensions.
  {
    // With axes = [1, 3].
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .attributes = {.axes = {1, 3}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 2, 2, 8}},
                     .expected = false}
        .Test();
  }
  {
    // With axes = [1, 2, 3]
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .attributes = {.axes = {1, 2, 3}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 2, 4, 8}},
                     .expected = false}
        .Test();
  }
  // Test the invalid graph when the dimension of output doesn't equal to
  // the dimension of input except along the axes.
  {
    // With explicit scales.
    Resample2dTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {1, 1, 2, 4}},
        .attributes = {.scales = std::vector<float>{2, 2}, .axes = {2, 3}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {1, 2, 4, 8}},
        .expected = false}
        .Test();
  }
  {
    // Without explicit scales.
    Resample2dTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 1, 2, 4}},
                     .attributes = {.axes = {2, 3}},
                     .output = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 2, 4, 8}},
                     .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput(
        "input", {1, 1, 2, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildResample2d(input_operand_id, input_operand_id,
                            Resample2dTester::Resample2dAttributes{});

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct ReshapeTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildReshape(input_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ReshapeTest) {
  {
    // Test reshape operator from 2-D tensor to 1-D tensor.
    ReshapeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 4}},
                  .output = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {8}},
                  .expected = true}
        .Test();
  }
  {
    // Test reshape operator from 4-D tensor to 2-D tensor.
    ReshapeTester{.input = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {1, 3, 2, 1}},
                  .output = {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {1, 6}},
                  .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when one value of new shape is 0.
    ReshapeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {4, 2}},
                  .output = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 0}},
                  .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the number of input elements are not
    // equal to the number of output elements.
    ReshapeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 3, 4}},
                  .output = {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {3, 5}},
                  .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    ReshapeTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
}
struct SliceTester {
  struct SliceAttributes {
    std::vector<uint32_t> starts;
    std::vector<uint32_t> sizes;
  };

  OperandInfo input;
  SliceAttributes attributes;
  OperandInfo output;

  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildSlice(input_operand_id, output_operand_id,
                       std::move(attributes.starts),
                       std::move(attributes.sizes));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, SliceTest) {
  {
    // Test slice with output dimensions equal to input dimensions.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 4}},
                .attributes = {.starts = {0, 0}, .sizes = {4, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {4, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test 4x4 2-D Tensor to 2x2 slice
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 4}},
                .attributes = {.starts = {0, 0}, .sizes = {2, 2}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 2}},
                .expected = true}
        .Test();
  }
  {
    // Test 4x4 2-D Tensor to 2x2 slice with offsets
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 4}},
                .attributes = {.starts = {2, 2}, .sizes = {2, 2}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 2}},
                .expected = true}
        .Test();
  }
  {
    // Test that going out-of-bounds of the input tensor fails.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {2, 2}},
                .attributes = {.starts = {1, 0}, .sizes = {2, 2}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 2}},
                .expected = false}
        .Test();
  }
  {
    // Test that mismatched output dimensions and size attribute will fail.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {2, 2}},
                .attributes = {.starts = {0, 0}, .sizes = {1, 1}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 1}},
                .expected = false}
        .Test();
  }
  {
    // Test that using size zero will result in failure.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {2, 2}},
                .attributes = {.starts = {0, 0}, .sizes = {0, 1}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {1}},
                .expected = false}
        .Test();
  }
  {
    // Test that having starts and sizes lengths not equal to the input rank
    // will fail.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                          .dimensions = {4, 4}},
                .attributes = {.starts = {0}, .sizes = {4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {4, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test that input data type not equal to the output data type will
    // fail.
    SliceTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                          .dimensions = {4, 4}},
                .attributes = {.starts = {0, 0}, .sizes = {4, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {4, 4}},
                .expected = false}
        .Test();
  }
}

enum class FloatingPointUnaryKind { kLeakyRelu, kLinear, kSigmoid, kTanh };

struct FloatingPointUnaryTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    Test(FloatingPointUnaryKind::kLeakyRelu);
    Test(FloatingPointUnaryKind::kLinear);
    Test(FloatingPointUnaryKind::kSigmoid);
    Test(FloatingPointUnaryKind::kTanh);
  }

  void Test(FloatingPointUnaryKind kind) {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    switch (kind) {
      case FloatingPointUnaryKind::kLeakyRelu:
        builder.BuildLeakyRelu(input_operand_id, output_operand_id,
                               /*alpha*/ 1.0);
        break;
      case FloatingPointUnaryKind::kLinear:
        builder.BuildLinear(input_operand_id, output_operand_id,
                            /*alpha*/ 1.0, /*beta*/ 0.0);
        break;
      case FloatingPointUnaryKind::kSigmoid:
        builder.BuildSigmoid(input_operand_id, output_operand_id);
        break;
      case FloatingPointUnaryKind::kTanh:
        builder.BuildTanh(input_operand_id, output_operand_id);
        break;
    }
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, FloatingPointUnaryTest) {
  {
    // Test the operator for 2-D tensor with float32 input.
    FloatingPointUnaryTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2, 6}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2, 6}},
        .expected = true}
        .Test();
  }
  {
    // Test the operator for 3-D tensor with float16 input.
    FloatingPointUnaryTester{
        .input = {.type = mojom::Operand::DataType::kFloat16,
                  .dimensions = {2, 6, 4}},
        .output = {.type = mojom::Operand::DataType::kFloat16,
                   .dimensions = {2, 6, 4}},
        .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not as expected.
    FloatingPointUnaryTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {4, 2}},
        .output = {.type = mojom::Operand::DataType::kFloat32,
                   .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output data types which don't match.
    FloatingPointUnaryTester{
        .input = {.type = mojom::Operand::DataType::kFloat32,
                  .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the input data type is not floating
    // point.
    FloatingPointUnaryTester{
        .input = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .output = {.type = mojom::Operand::DataType::kInt32, .dimensions = {2}},
        .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for leaky relu when the input is as same as
    // output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildLeakyRelu(input_operand_id, input_operand_id,
                           /*alpha*/ 1.0);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for leaky relu when alpha is NAN.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t output_operand_id =
        builder.BuildOutput("output", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildLeakyRelu(input_operand_id, output_operand_id,
                           /*alpha*/ NAN);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for linear when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildLinear(input_operand_id, input_operand_id,
                        /*alpha*/ 1.0, /*beta*/ 0.0);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for linear when alpha is NAN.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t output_operand_id =
        builder.BuildOutput("output", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildLinear(input_operand_id, output_operand_id,
                        /*alpha*/ NAN, /*beta*/ 0.0);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for linear when beta is NAN.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    uint64_t output_operand_id =
        builder.BuildOutput("output", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildLinear(input_operand_id, output_operand_id,
                        /*alpha*/ 1.0, /*beta*/ NAN);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for sigmoid when the input is as same as
    // output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildSigmoid(input_operand_id, input_operand_id);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph for tanh when the input is as same as output.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {2}, mojom::Operand::DataType::kFloat32);
    builder.BuildTanh(input_operand_id, input_operand_id);

    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct SoftmaxTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildSoftmax(input_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, SoftmaxTest) {
  {
    // Test softmax operator for input operand with [2, 2] dimensions.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 2}},
                  .output = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 2}},
                  .expected = true}
        .Test();
  }
  {
    // Test softmax operator for input operand with [1, 4] dimensions.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                            .dimensions = {1, 4}},
                  .output = {.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {1, 4}},
                  .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when building softmax with 4-D input.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {1, 1, 4, 2}},
                  .output = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {1, 1, 4, 2}},
                  .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when building softmax with int32 input.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kInt32,
                            .dimensions = {2, 3}},
                  .output = {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {2, 3}},
                  .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {4, 2}},
                  .output = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2}},
                  .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    SoftmaxTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                            .dimensions = {2, 5}},
                  .output = {.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {2, 5}},
                  .expected = false}
        .Test();
  }
}

struct SoftplusTester {
  OperandInfo input;
  OperandInfo output;
  float steepness = 1.0;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildSoftplus(input_operand_id, output_operand_id, steepness);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, SoftplusTest) {
  {
    // Test softplus operator with `steepness` = 1.5.
    SoftplusTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 2}},
                   .output = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {2, 2}},
                   .steepness = 1.5,
                   .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for invalid data type.
    SoftplusTester{.input = {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {4, 2}},
                   .output = {.type = mojom::Operand::DataType::kInt32,
                              .dimensions = {4, 2}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for `steepness` is NAN.
    SoftplusTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {4, 2}},
                   .output = {.type = mojom::Operand::DataType::kFloat16,
                              .dimensions = {4, 2}},
                   .steepness = NAN,
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    SoftplusTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {4, 2}},
                   .output = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {2}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    SoftplusTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 5}},
                   .output = {.type = mojom::Operand::DataType::kFloat16,
                              .dimensions = {2, 5}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {4, 6}, mojom::Operand::DataType::kFloat32);

    builder.BuildSoftplus(input_operand_id, input_operand_id,
                          /*steepness*/ 1.0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct SoftsignTester {
  OperandInfo input;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildSoftsign(input_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, SoftsignTest) {
  {
    // Test softsign operator with input dimensions = [2, 4] and data type
    // float32.
    SoftsignTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 4}},
                   .output = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {2, 4}},
                   .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for invalid data type.
    SoftsignTester{.input = {.type = mojom::Operand::DataType::kInt32,
                             .dimensions = {4, 2}},
                   .output = {.type = mojom::Operand::DataType::kInt32,
                              .dimensions = {4, 2}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for the output shapes are not expected.
    SoftsignTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {4, 2}},
                   .output = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {2}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    SoftsignTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                             .dimensions = {2, 5}},
                   .output = {.type = mojom::Operand::DataType::kFloat16,
                              .dimensions = {2, 5}},
                   .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for input operand == output operand.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", {4, 6}, mojom::Operand::DataType::kFloat32);
    builder.BuildSoftsign(input_operand_id, input_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct SplitTester {
  OperandInfo input;
  std::vector<OperandInfo> outputs;
  uint32_t axis = 0;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);

    std::vector<uint64_t> output_operand_ids;
    for (size_t i = 0; i < outputs.size(); ++i) {
      output_operand_ids.push_back(
          builder.BuildOutput("output" + base::NumberToString(i),
                              outputs[i].dimensions, outputs[i].type));
    }
    builder.BuildSplit(input_operand_id, output_operand_ids, axis);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ValidateSplitTest) {
  using mojom::Operand::DataType::kFloat32;
  {
    // Tests default axis split.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 2}},
                .outputs = {{.type = kFloat32, .dimensions = {1, 2}},
                            {.type = kFloat32, .dimensions = {1, 2}}},
                .expected = true}
        .Test();
  }
  {
    // Tests axis=1 split.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 2}},
                .outputs = {{.type = kFloat32, .dimensions = {2, 1}},
                            {.type = kFloat32, .dimensions = {2, 1}}},
                .axis = 1,
                .expected = true}
        .Test();
  }
  {
    // Tests for an invalid graph where not all output types match the input
    // type.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 2}},
                .outputs = {{.type = kFloat32, .dimensions = {1, 2}},
                            {.type = mojom::Operand::DataType::kFloat16,
                             .dimensions = {1, 2}}},
                .expected = false}
        .Test();
  }
  {
    // Tests for an invalid graph where the sum of the splits is less than
    // the input tensor size.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 6}},
                .outputs = {{.type = kFloat32, .dimensions = {2, 1}},
                            {.type = kFloat32, .dimensions = {2, 2}},
                            {.type = kFloat32, .dimensions = {2, 2}}},
                .axis = 1,
                .expected = false}
        .Test();
  }
  {
    // Tests for an invalid graph where the sum of the splits is greater
    // than the input tensor size.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 6}},
                .outputs = {{.type = kFloat32, .dimensions = {2, 1}},
                            {.type = kFloat32, .dimensions = {2, 2}},
                            {.type = kFloat32, .dimensions = {2, 4}}},
                .axis = 1,
                .expected = false}
        .Test();
  }
  {
    // Tests for an invalid graph where specified axis is greater then the
    // rank of the input tensor
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 2}},
                .outputs = {{.type = kFloat32, .dimensions = {1, 2}},
                            {.type = kFloat32, .dimensions = {1, 2}}},
                .axis = 2,
                .expected = false}
        .Test();
  }
  {
    // Tests for an invalid graph where a split of size 0 is specified.
    SplitTester{.input = {.type = kFloat32, .dimensions = {2, 2}},
                .outputs = {{.type = kFloat32, .dimensions = {0, 2}},
                            {.type = kFloat32, .dimensions = {2, 2}}},
                .expected = false}
        .Test();
  }
  {
    // Tests for an invalid graph where a split as specified along multiple
    // axis.
    SplitTester{.input = {.type = kFloat32, .dimensions = {4, 6}},
                .outputs = {{.type = kFloat32, .dimensions = {1, 2}},
                            {.type = kFloat32, .dimensions = {2, 3}},
                            {.type = kFloat32, .dimensions = {1, 1}}},
                .expected = false}
        .Test();
  }
  {
    GraphInfoBuilder builder;
    uint64_t input_operand_id = builder.BuildInput("input", {4, 6}, kFloat32);

    builder.BuildSplit(input_operand_id, {input_operand_id}, 0);
    builder.BuildSplit(input_operand_id,
                       {builder.BuildOutput("output", {4, 6}, kFloat32)}, 0);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

struct TransposeTester {
  OperandInfo input;
  std::vector<uint32_t> permutation;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t input_operand_id =
        builder.BuildInput("input", input.dimensions, input.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildTranspose(input_operand_id, output_operand_id,
                           std::move(permutation));
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, TransposeTest) {
  {
    // Test transpose operator with permutation [2, 3, 1, 0].
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {1, 2, 3, 4}},
                    .permutation = {2, 3, 1, 0},
                    .output = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {3, 4, 2, 1}},
                    .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the rank of permutation is larger than
    // the input rank.
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {1, 2, 3}},
                    .permutation = {0, 1, 2, 2},
                    .output = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 2, 3, 3}},
                    .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the permutation contains duplicate
    // values.
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {1, 2, 3, 4}},
                    .permutation = {0, 1, 2, 2},
                    .output = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 2, 3, 3}},
                    .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when one value in permutation is greater than
    // input_rank - 1.
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat16,
                              .dimensions = {1, 2, 3, 4}},
                    .permutation = {0, 1, 2, 4},
                    .output = {.type = mojom::Operand::DataType::kFloat16,
                               .dimensions = {1, 2, 3, 4}},
                    .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output shapes are not expected.
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {1, 2, 3, 4}},
                    .permutation = {0, 1, 2, 3},
                    .output = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {1, 2, 3}},
                    .expected = false}
        .Test();
  }
  {
    // Test the invalid graph for output types don't match.
    TransposeTester{.input = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {1, 2, 3, 4}},
                    .permutation = {0, 1, 2, 3},
                    .output = {.type = mojom::Operand::DataType::kFloat16,
                               .dimensions = {1, 2, 3, 4}},
                    .expected = false}
        .Test();
  }
}

struct WhereTester {
  OperandInfo condition;
  OperandInfo true_value;
  OperandInfo false_value;
  OperandInfo output;
  bool expected;

  void Test() {
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t condition_operand_id =
        builder.BuildInput("condition", condition.dimensions, condition.type);
    uint64_t true_value_operand_id = builder.BuildInput(
        "true_value", true_value.dimensions, true_value.type);
    uint64_t false_value_operand_id = builder.BuildInput(
        "false_value", false_value.dimensions, false_value.type);
    uint64_t output_operand_id =
        builder.BuildOutput("output", output.dimensions, output.type);
    builder.BuildWhere(condition_operand_id, true_value_operand_id,
                       false_value_operand_id, output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, WhereTest) {
  {
    // Test the invalid graph when the condition data type is not uint8.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kFloat32,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the the data types of true_value and
    // false_value don't match.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat16,
                                .dimensions = {2, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the the data types of output and
    // true_value don't match.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat16,
                           .dimensions = {2, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the the shape of output is wrong.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 5}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the shapes of true_value and false_value
    // are not broadcastable.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test the invalid graph when the condition shape is not broadcastable.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 3}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 1}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 4}},
                .expected = false}
        .Test();
  }
  {
    // Test where with 2-D condition, 2-D true_value and 2-D false_value using
    // broadcast.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 1}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test where with 2-D condition, 2-D true_value and 3-D false_value using
    // broadcast.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {1, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {3, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {2, 3, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test where with 3-D condition, 3-D true_value and 3-D false_value using
    // broadcast.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 1, 4}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {2, 3, 4}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {1, 4}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4}},
                .expected = true}
        .Test();
  }
  {
    // Test where with 4-D condition, 3-D true_value and 2-D false_value using
    // broadcast.
    WhereTester{.condition = {.type = mojom::Operand::DataType::kUint8,
                              .dimensions = {2, 3, 4, 5}},
                .true_value = {.type = mojom::Operand::DataType::kFloat32,
                               .dimensions = {3, 4, 5}},
                .false_value = {.type = mojom::Operand::DataType::kFloat32,
                                .dimensions = {4, 5}},
                .output = {.type = mojom::Operand::DataType::kFloat32,
                           .dimensions = {2, 3, 4, 5}},
                .expected = true}
        .Test();
  }
  {
    // Test the invalid graph when the condition is as same as output.
    GraphInfoBuilder builder;
    uint64_t condition_operand_id = builder.BuildInput(
        "condition", {2, 4}, mojom::Operand::DataType::kUint8);
    uint64_t true_value_operand_id = builder.BuildInput(
        "true_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t false_value_operand_id = builder.BuildInput(
        "false_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildWhere(condition_operand_id, true_value_operand_id,
                       false_value_operand_id, condition_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the true_value is as same as output.
    GraphInfoBuilder builder;
    uint64_t condition_operand_id = builder.BuildInput(
        "condition", {2, 4}, mojom::Operand::DataType::kUint8);
    uint64_t true_value_operand_id = builder.BuildInput(
        "true_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t false_value_operand_id = builder.BuildInput(
        "false_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildWhere(condition_operand_id, true_value_operand_id,
                       false_value_operand_id, true_value_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
  {
    // Test the invalid graph when the false_value is as same as output.
    GraphInfoBuilder builder;
    uint64_t condition_operand_id = builder.BuildInput(
        "condition", {2, 4}, mojom::Operand::DataType::kUint8);
    uint64_t true_value_operand_id = builder.BuildInput(
        "true_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    uint64_t false_value_operand_id = builder.BuildInput(
        "false_value", {2, 4}, mojom::Operand::DataType::kFloat32);
    builder.BuildWhere(condition_operand_id, true_value_operand_id,
                       false_value_operand_id, false_value_operand_id);
    EXPECT_FALSE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
  }
}

TEST_F(WebNNGraphImplTest, ValidateInputsTest) {
  const std::vector<uint32_t> dimensions = {3, 5};
  // Build the graph with mojo type.
  GraphInfoBuilder builder;
  uint64_t lhs_operand_id =
      builder.BuildInput("lhs", dimensions, mojom::Operand::DataType::kUint8);
  uint64_t rhs_operand_id =
      builder.BuildInput("rhs", dimensions, mojom::Operand::DataType::kUint8);
  uint64_t output_operand_id = builder.BuildOutput(
      "output", dimensions, mojom::Operand::DataType::kUint8);
  builder.BuildElementWiseBinary(mojom::ElementWiseBinary::Kind::kAdd,
                                 lhs_operand_id, rhs_operand_id,
                                 output_operand_id);
  EXPECT_TRUE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));

  auto byte_length =
      ValidateAndCalculateByteLength(sizeof(uint8_t), dimensions).value();
  {
    // Validate the inputs match the expected.
    base::flat_map<std::string, mojo_base::BigBuffer> inputs;
    inputs["lhs"] = std::vector<uint8_t>(byte_length);
    inputs["rhs"] = std::vector<uint8_t>(byte_length);
    EXPECT_TRUE(ValidateInputsForComputing(builder.CloneGraphInfo(),
                                           std::move(inputs)));
  }
  {
    // Test the invalid inputs for invalid input size.
    base::flat_map<std::string, mojo_base::BigBuffer> inputs;
    inputs["lhs"] = std::vector<uint8_t>(byte_length);
    EXPECT_FALSE(ValidateInputsForComputing(builder.CloneGraphInfo(),
                                            std::move(inputs)));
  }
  {
    // Test the invalid inputs for invalid input name.
    base::flat_map<std::string, mojo_base::BigBuffer> inputs;
    inputs["a_different_input_name"] = std::vector<uint8_t>(byte_length);
    inputs["rhs"] = std::vector<uint8_t>(byte_length);
    EXPECT_FALSE(ValidateInputsForComputing(builder.CloneGraphInfo(),
                                            std::move(inputs)));
  }
  {
    // Test the invalid inputs for invalid first input byte length.
    base::flat_map<std::string, mojo_base::BigBuffer> inputs;
    inputs["lhs"] = std::vector<uint8_t>(20);
    inputs["rhs"] = std::vector<uint8_t>(byte_length);
    EXPECT_FALSE(ValidateInputsForComputing(builder.CloneGraphInfo(),
                                            std::move(inputs)));
  }
  {
    // Test the invalid inputs for invalid second input byte length.
    base::flat_map<std::string, mojo_base::BigBuffer> inputs;
    inputs["lhs"] = std::vector<uint8_t>(byte_length);
    inputs["rhs"] = std::vector<uint8_t>(20);
    EXPECT_FALSE(ValidateInputsForComputing(builder.CloneGraphInfo(),
                                            std::move(inputs)));
  }
}

struct ConstantOperandTester {
  std::vector<uint8_t> values;
  bool expected;

  void Test() {
    const std::vector<uint32_t> dimensions = {3, 5};
    // Build the graph with mojo type.
    GraphInfoBuilder builder;
    uint64_t lhs_operand_id =
        builder.BuildInput("lhs", dimensions, mojom::Operand::DataType::kUint8);
    uint64_t rhs_operand_id = builder.BuildConstant(
        dimensions, mojom::Operand::DataType::kUint8, values);
    uint64_t output_operand_id = builder.BuildOutput(
        "output", dimensions, mojom::Operand::DataType::kUint8);
    builder.BuildElementWiseBinary(mojom::ElementWiseBinary::Kind::kAdd,
                                   lhs_operand_id, rhs_operand_id,
                                   output_operand_id);
    EXPECT_EQ(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()), expected);
  }
};

TEST_F(WebNNGraphImplTest, ValidateConstantOperandTest) {
  {
    // Test valid constant data.
    ConstantOperandTester{.values = std::vector<uint8_t>(15), .expected = true}
        .Test();
  }
  {
    // Test the invalid graph for the byte length of constant data doesn't
    // match the graph's expected.
    ConstantOperandTester{.values = std::vector<uint8_t>(10), .expected = false}
        .Test();
  }
}

// Test building a graph with two inputs and two constant in the following
// topology.
//    [input_a] [constant_a] [input_b] [constant_b]
//           \    /                \    /
//            gemm                  gemm
//                \                /
//                       gemm
TEST_F(WebNNGraphImplTest, BuildMultipleInputsAppendingConstants) {
  // Build the mojom graph info.
  GraphInfoBuilder builder;
  // The graph outputs are built first, and then inputs / constants.
  uint64_t output_operand_id =
      builder.BuildOutput("output", {2, 2}, mojom::Operand::DataType::kFloat32);
  uint64_t input_a_operand_id =
      builder.BuildInput("input_a", {2, 2}, mojom::Operand::DataType::kFloat32);
  std::vector<float> constant_data = {5.0, 6.0, 7.0, 8.0};
  uint64_t constant_a_operand_id =
      builder.BuildConstant({2, 2}, mojom::Operand::DataType::kFloat32,
                            base::as_byte_span(constant_data));

  uint64_t intermediate_1_operand_id = builder.BuildIntermediateOperand(
      {2, 2}, mojom::Operand::DataType::kFloat32);
  builder.BuildGemm(input_a_operand_id, constant_a_operand_id,
                    intermediate_1_operand_id, GemmTester::GemmAttributes());

  uint64_t input_b_operand_id =
      builder.BuildInput("input_b", {2, 2}, mojom::Operand::DataType::kFloat32);
  uint64_t constant_b_operand_id =
      builder.BuildConstant({2, 2}, mojom::Operand::DataType::kFloat32,
                            base::as_byte_span(constant_data));
  uint64_t intermediate_2_operand_id = builder.BuildIntermediateOperand(
      {2, 2}, mojom::Operand::DataType::kFloat32);
  builder.BuildGemm(input_b_operand_id, constant_b_operand_id,
                    intermediate_2_operand_id, GemmTester::GemmAttributes());
  builder.BuildGemm(intermediate_1_operand_id, intermediate_2_operand_id,
                    output_operand_id, GemmTester::GemmAttributes());
  EXPECT_TRUE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
}

// Test building a graph with two inputs and two constant in the following
// topology.
//    [constant_a] [input_a] [constant_b] [input_b]
//           \    /                \    /
//            gemm                  gemm
//                \                /
//                       gemm
TEST_F(WebNNGraphImplTest, BuildMultipleConstantsAppendingInputs) {
  // Build the mojom graph info.
  GraphInfoBuilder builder;
  // The graph outputs are built first, and then inputs / constants.
  uint64_t output_operand_id =
      builder.BuildOutput("output", {2, 2}, mojom::Operand::DataType::kFloat32);
  std::vector<float> constant_data = {5.0, 6.0, 7.0, 8.0};
  uint64_t constant_a_operand_id =
      builder.BuildConstant({2, 2}, mojom::Operand::DataType::kFloat32,
                            base::as_byte_span(constant_data));
  uint64_t input_a_operand_id =
      builder.BuildInput("input_a", {2, 2}, mojom::Operand::DataType::kFloat32);
  uint64_t intermediate_1_operand_id = builder.BuildIntermediateOperand(
      {2, 2}, mojom::Operand::DataType::kFloat32);
  builder.BuildGemm(constant_a_operand_id, input_a_operand_id,
                    intermediate_1_operand_id, GemmTester::GemmAttributes());

  uint64_t input_b_operand_id =
      builder.BuildInput("input_b", {2, 2}, mojom::Operand::DataType::kFloat32);
  uint64_t constant_b_operand_id =
      builder.BuildConstant({2, 2}, mojom::Operand::DataType::kFloat32,
                            base::as_byte_span(constant_data));
  uint64_t intermediate_2_operand_id = builder.BuildIntermediateOperand(
      {2, 2}, mojom::Operand::DataType::kFloat32);
  builder.BuildGemm(constant_b_operand_id, input_b_operand_id,
                    intermediate_2_operand_id, GemmTester::GemmAttributes());

  builder.BuildGemm(intermediate_1_operand_id, intermediate_2_operand_id,
                    output_operand_id, GemmTester::GemmAttributes());
  EXPECT_TRUE(WebNNGraphImpl::ValidateGraph(builder.GetGraphInfo()));
}

}  // namespace webnn
