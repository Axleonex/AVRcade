#include "adapters/unreal/d3d12_observer.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"
#include "adapters/unreal/native_unreal_contract.h"
#include "adapters/unreal/unreal_camera_contract.h"
#include "adapters/unreal/unreal_camera_observer.h"
#include "adapters/unreal/meccha_camera_identity_gate.h"
#include "adapters/unreal/meccha_camera_pin.h"
#include "adapters/unreal/meccha_orientation_service.h"
#include "adapters/unreal/meccha_orientation_tracking_gate.h"
#include "adapters/unreal/meccha_stereo_pose_pair.h"
#include "adapters/unreal/meccha_stereo_projection.h"
#include "adapters/unreal/unreal_frame_contract.h"
#include "adapters/unreal/unreal_openxr_bridge.h"
#include "adapters/unreal/unreal_orientation_only.h"
#include "adapters/unreal/unreal_scene_view_decoder.h"
#include "adapters/unreal/unreal_view_seam_locator.h"
#include "adapters/unreal/unreal_view_seam_pe.h"
#include "openxr/d3d12_binding.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct GraphicsFixture {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> direct_queue;
  ComPtr<ID3D12CommandQueue> compute_queue;
  ComPtr<IDXGISwapChain3> swapchain;
};

bool decodeSyntheticInstruction(
    const std::uint8_t* instruction,
    std::size_t available,
    std::uint32_t instruction_rva,
    vrclient::adapters::unreal::UnrealDecodedInstruction* output,
    void*) {
  using namespace vrclient::adapters::unreal;
  if (instruction == nullptr || output == nullptr || available == 0) {
    return false;
  }
  *output = {};
  if (instruction[0] == 0xE8 && available >= 5) {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, instruction + 1, sizeof(displacement));
    output->length = 5;
    output->flow = UnrealInstructionFlow::Call;
    output->has_target = true;
    output->target_rva = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(instruction_rva) + 5 + displacement);
  } else if (instruction[0] == 0xC3) {
    output->length = 1;
    output->flow = UnrealInstructionFlow::Return;
  } else if (available >= 7 && instruction[0] == 0x48 &&
             instruction[1] == 0x8D) {
    output->length = 7;
  } else {
    output->length = 1;
  }
  return true;
}

ComPtr<ID3D12CommandQueue> makeQueue(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type) {
  D3D12_COMMAND_QUEUE_DESC desc{};
  desc.Type = type;
  ComPtr<ID3D12CommandQueue> queue;
  expect(
      SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))),
      "CreateCommandQueue failed");
  return queue;
}

GraphicsFixture makeGraphics() {
  GraphicsFixture out;
  expect(
      SUCCEEDED(D3D12CreateDevice(
          nullptr,
          D3D_FEATURE_LEVEL_11_0,
          IID_PPV_ARGS(&out.device))),
      "D3D12CreateDevice failed");
  out.direct_queue = makeQueue(out.device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
  out.compute_queue = makeQueue(out.device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);

  ComPtr<IDXGIFactory4> factory;
  expect(
      SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),
      "CreateDXGIFactory1 failed");
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = 16;
  desc.Height = 8;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> swapchain1;
  expect(
      SUCCEEDED(factory->CreateSwapChainForComposition(
          out.direct_queue.Get(),
          &desc,
          nullptr,
          &swapchain1)),
      "CreateSwapChainForComposition failed");
  expect(
      SUCCEEDED(swapchain1.As(&out.swapchain)),
      "IDXGISwapChain3 unavailable");
  return out;
}

void testReadinessContract() {
  using namespace vrclient::adapters::unreal;
  NativeUnrealProfile profile{
      "meccha-chameleon",
      "steam-4704690-build-24489781",
      "PenguinHotel-Win64-Shipping.exe",
      std::string(64, 'a'),
      RendererApi::D3D12,
      true,
  };
  NativeUnrealTarget target{
      profile.game_id,
      profile.build_id,
      "C:\\Games\\Meccha\\PenguinHotel-Win64-Shipping.exe",
      profile.expected_sha256,
      false,
  };
  UnrealGraphicsObservation graphics{RendererApi::D3D12, true, true, true};
  expect(
      evaluateNativeUnrealReadiness(profile, target, graphics) ==
          NativeUnrealReadiness::Ready,
      "valid native route should be ready");

  auto unpinned = profile;
  unpinned.expected_sha256.clear();
  expect(
      evaluateNativeUnrealReadiness(unpinned, target, graphics) ==
          NativeUnrealReadiness::FingerprintMissing,
      "unpinned profile must fail closed");

  auto wrong_hash = target;
  wrong_hash.executable_sha256 = std::string(64, 'b');
  expect(
      evaluateNativeUnrealReadiness(profile, wrong_hash, graphics) ==
          NativeUnrealReadiness::HashMismatch,
      "wrong executable hash must fail closed");

  auto converter = target;
  converter.external_converter_present = true;
  expect(
      evaluateNativeUnrealReadiness(profile, converter, graphics) ==
          NativeUnrealReadiness::ExternalConverterPresent,
      "external converter must block the native route");

  auto missing_swapchain = graphics;
  missing_swapchain.swapchain_present = false;
  expect(
      evaluateNativeUnrealReadiness(profile, target, missing_swapchain) ==
          NativeUnrealReadiness::MissingSwapchain,
      "missing swapchain must block the native route");
}

void testDiscoveryAndSubmissionContracts() {
  using namespace vrclient::adapters::unreal;
  const UnrealObjectSelector selector{
      "/Game/Maps/Test.PlayerCamera",
      "CameraComponent",
      true,
  };
  std::vector<UnrealObjectCandidate> candidates = {
      {7, selector.object_path, selector.component_name, true},
      {8, "/Game/Maps/Test.OtherCamera", selector.component_name, true},
  };
  std::uint64_t object_id = 0;
  expect(
      selectUniqueUnrealObject(selector, candidates, &object_id) ==
              UnrealDiscoveryResult::Found &&
          object_id == 7,
      "exact active camera selector should resolve once");

  auto missing = selector;
  missing.object_path = "/Game/Maps/Test.Missing";
  expect(
      selectUniqueUnrealObject(missing, candidates, &object_id) ==
          UnrealDiscoveryResult::Missing,
      "missing camera selector must fail closed");

  candidates.push_back({9, selector.object_path, selector.component_name, true});
  expect(
      selectUniqueUnrealObject(selector, candidates, &object_id) ==
          UnrealDiscoveryResult::Ambiguous,
      "ambiguous camera selector must fail closed");

  UnrealFrameSubmission submission{};
  submission.frame_index = 1;
  submission.predicted_display_time_ns = 1234;
  submission.renderer = RendererApi::D3D12;
  submission.eyes[0].color_target = reinterpret_cast<void*>(1);
  submission.eyes[1].color_target = reinterpret_cast<void*>(2);
  for (auto& eye : submission.eyes) {
    eye.width = 16;
    eye.height = 8;
    eye.color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    eye.view.values[0] = 1.0f;
    eye.projection.values[0] = 1.0f;
  }
  expect(
      validateUnrealFrameSubmission(submission) ==
          UnrealSubmissionResult::Ready,
      "valid two-eye submission should pass");

  auto shared = submission;
  shared.eyes[1].color_target = shared.eyes[0].color_target;
  expect(
      validateUnrealFrameSubmission(shared) ==
          UnrealSubmissionResult::SharedEyeTarget,
      "one-resource stereo must fail closed");

  auto non_finite = submission;
  non_finite.eyes[1].view.values[3] =
      std::numeric_limits<float>::quiet_NaN();
  expect(
      validateUnrealFrameSubmission(non_finite) ==
          UnrealSubmissionResult::NonFiniteMatrix,
      "non-finite camera matrix must fail closed");
}

void testObserverAndBorrowedBinding() {
  using namespace vrclient::adapters::unreal;
  using namespace vrclient::runtime::openxr;
  auto graphics = makeGraphics();

  D3D12Observer observer;
  expect(
      observer.observeDeviceQueue(
          graphics.device.Get(),
          graphics.compute_queue.Get()) ==
          D3D12ObservationResult::QueueNotDirect,
      "compute queue must be rejected");
  expect(
      observer.observeDeviceQueue(
          graphics.device.Get(),
          graphics.direct_queue.Get()) ==
          D3D12ObservationResult::Pending,
      "direct device/queue should wait for swapchain");
  expect(
      observer.observeSwapchain(graphics.swapchain.Get()) ==
          D3D12ObservationResult::Ready,
      "matching swapchain should make observation ready");
  observer.recordPresent();
  observer.recordResize();
  const auto snapshot = observer.snapshot();
  expect(snapshot.ready, "observer snapshot should be ready");
  expect(snapshot.width == 16 && snapshot.height == 8, "swapchain size mismatch");
  expect(snapshot.buffer_count == 2, "swapchain buffer count mismatch");
  expect(
      snapshot.present_count == 1 && snapshot.resize_count == 1,
      "observer counters mismatch");

  D3D12DeviceContext adopted;
  expect(
      adoptBorrowedD3D12Binding(
          graphics.device->GetAdapterLuid(),
          D3D_FEATURE_LEVEL_11_0,
          observer.borrowedBinding(),
          &adopted) == D3D12BindingAdoptResult::Ready,
      "matching game device/queue should be adopted");
  expect(adopted.borrowed, "adopted context must record borrowed ownership");
  expect(
      adopted.device.Get() == graphics.device.Get() &&
          adopted.queue.Get() == graphics.direct_queue.Get(),
      "adopted binding changed COM objects");

  LUID wrong_luid = graphics.device->GetAdapterLuid();
  ++wrong_luid.LowPart;
  expect(
      adoptBorrowedD3D12Binding(
          wrong_luid,
          D3D_FEATURE_LEVEL_11_0,
          observer.borrowedBinding(),
          &adopted) == D3D12BindingAdoptResult::AdapterMismatch,
      "wrong OpenXR adapter must be rejected");

  expect(
      adoptBorrowedD3D12Binding(
          graphics.device->GetAdapterLuid(),
          D3D_FEATURE_LEVEL_11_0,
          {graphics.device.Get(), graphics.compute_queue.Get()},
          &adopted) == D3D12BindingAdoptResult::QueueNotDirect,
      "borrowed compute queue must be rejected");

  observer.reset();
  expect(!observer.snapshot().ready, "reset must release observation state");
}

void testLiveCameraSampleContract() {
  using namespace vrclient::adapters::unreal;

  UnrealCameraSample sample{};
  sample.camera_id = 7;
  sample.sample_index = 42;
  sample.sample_time_ns = 9'000'000;
  sample.active = true;
  sample.location_uu = {100.0, -25.0, 175.0};
  sample.rotation_degrees = {-5.0, 90.0, 0.0};
  sample.vertical_fov_degrees = 80.0F;
  sample.aspect_ratio = 16.0F / 9.0F;
  sample.near_clip_uu = 10.0F;
  sample.far_clip_uu = 100'000.0F;

  expect(
      validateUnrealCameraSample(sample, 10'000'000, 2'000'000) ==
          UnrealCameraSampleResult::Ready,
      "fresh active camera sample should be ready");

  auto stale = sample;
  stale.sample_time_ns = 7'000'000;
  expect(
      validateUnrealCameraSample(stale, 10'000'000, 2'000'000) ==
          UnrealCameraSampleResult::Stale,
      "stale camera data must fail closed");

  auto non_finite = sample;
  non_finite.rotation_degrees.y =
      std::numeric_limits<double>::quiet_NaN();
  expect(
      validateUnrealCameraSample(non_finite, 10'000'000, 2'000'000) ==
          UnrealCameraSampleResult::NonFiniteTransform,
      "non-finite camera transform must fail closed");

  sample.rotation_degrees = {};

  VrRuntimePose reference_head{};
  reference_head.orientation.w = 1.0F;
  reference_head.orientation_valid = 1;
  reference_head.position_valid = 1;

  VrRuntimeFrameData frame{};
  frame.timing.predicted_display_time_ns = 10'000'000;
  frame.eye_count = 2;
  frame.head_pose = reference_head;
  frame.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
  frame.eyes[1].eye = VR_RUNTIME_EYE_RIGHT;
  for (auto& eye : frame.eyes) {
    eye.pose.orientation.w = 1.0F;
    eye.pose.orientation_valid = 1;
    eye.pose.position_valid = 1;
  }
  frame.eyes[0].pose.position.x = -0.032F;
  frame.eyes[1].pose.position.x = 0.032F;

  UnrealStereoCameraFrame stereo{};
  expect(
      composeUnrealStereoCameraFrame(
          sample, reference_head, frame, 100.0, 2'000'000, &stereo) ==
          UnrealStereoCameraResult::Ready,
      "valid OpenXR eyes should compose into Unreal stereo cameras");
  expect(
      std::abs(stereo.eyes[0].location_uu.y - (-28.2)) < 0.001 &&
          std::abs(stereo.eyes[1].location_uu.y - (-21.8)) < 0.001 &&
          stereo.eyes[0].location_uu.y != stereo.eyes[1].location_uu.y,
      "OpenXR IPD must create distinct Unreal left/right camera locations");

  constexpr float kSqrtHalf = 0.70710678F;
  frame.head_pose.orientation = {0.0F, kSqrtHalf, 0.0F, kSqrtHalf};
  for (auto& eye : frame.eyes) {
    eye.pose.orientation = frame.head_pose.orientation;
    eye.pose.position = {};
  }
  expect(
      composeUnrealStereoCameraFrame(
          sample, reference_head, frame, 100.0, 2'000'000, &stereo) ==
              UnrealStereoCameraResult::Ready &&
          std::abs(stereo.eyes[0].rotation_degrees.y - (-90.0)) < 0.001 &&
          std::abs(stereo.eyes[1].rotation_degrees.y - (-90.0)) < 0.001,
      "OpenXR positive-Y yaw must map to Unreal negative yaw");
}

void testNeutralOrientationOnlyPosePreservesView() {
  using namespace vrclient::adapters::unreal;

  VrRuntimePose reference{};
  reference.orientation.w = 1.0F;
  reference.orientation_valid = 1;
  VrRuntimePose current = reference;

  UnrealOrientationPoseSample pose{};
  expect(
      makeUnrealOrientationPoseSample(reference, current, 7, 1'000, &pose) ==
          UnrealOrientationPoseResult::Ready,
      "neutral valid headset orientation should produce a pose sample");

  UnrealRotationMatrix3d view{{
      1.0, 2.0, 3.0,
      4.0, 5.0, 6.0,
      7.0, 8.0, 9.0,
  }};
  UnrealRotationMatrix3d output{};
  expect(
      applyUnrealOrientationOnlyPose(view, pose, 1'050, 100, &output) ==
              UnrealOrientationPoseResult::Ready &&
          output.values == view.values,
      "neutral orientation-only pose must preserve the existing game view");
}

vrclient::adapters::unreal::UnrealRotationMatrix3d multiplyOrientationFixture(
    const vrclient::adapters::unreal::UnrealRotationMatrix3d& left,
    const vrclient::adapters::unreal::UnrealRotationMatrix3d& right) {
  vrclient::adapters::unreal::UnrealRotationMatrix3d output{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t inner = 0; inner < 3; ++inner) {
        output.values[row * 3 + column] +=
            left.values[row * 3 + inner] *
            right.values[inner * 3 + column];
      }
    }
  }
  return output;
}

bool orientationMatricesNear(
    const vrclient::adapters::unreal::UnrealRotationMatrix3d& left,
    const vrclient::adapters::unreal::UnrealRotationMatrix3d& right,
    double tolerance = 1.0e-6) {
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    if (std::abs(left.values[index] - right.values[index]) > tolerance) {
      return false;
    }
  }
  return true;
}

vrclient::adapters::unreal::UnrealRotationMatrix3d unrealPitchFixture(
    double degrees) {
  constexpr double kRadiansPerDegree =
      3.14159265358979323846 / 180.0;
  const double radians = degrees * kRadiansPerDegree;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {{
      cosine, 0.0, -sine,
      0.0, 1.0, 0.0,
      sine, 0.0, cosine,
  }};
}

vrclient::adapters::unreal::UnrealRotationMatrix3d unrealYawFixture(
    double degrees) {
  constexpr double kRadiansPerDegree =
      3.14159265358979323846 / 180.0;
  const double radians = degrees * kRadiansPerDegree;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {{
      cosine, -sine, 0.0,
      sine, cosine, 0.0,
      0.0, 0.0, 1.0,
  }};
}

vrclient::adapters::unreal::UnrealRotationMatrix3d unrealRollFixture(
    double degrees) {
  constexpr double kRadiansPerDegree =
      3.14159265358979323846 / 180.0;
  const double radians = degrees * kRadiansPerDegree;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {{
      1.0, 0.0, 0.0,
      0.0, cosine, sine,
      0.0, -sine, cosine,
  }};
}

VrRuntimePose openXrAxisRotationFixture(
    double x, double y, double z, double degrees) {
  constexpr double kRadiansPerDegree =
      3.14159265358979323846 / 180.0;
  const double half_angle = degrees * kRadiansPerDegree * 0.5;
  const double sine = std::sin(half_angle);
  VrRuntimePose pose{};
  pose.orientation = {
      static_cast<float>(x * sine),
      static_cast<float>(y * sine),
      static_cast<float>(z * sine),
      static_cast<float>(std::cos(half_angle)),
  };
  pose.orientation_valid = 1;
  return pose;
}

void testOrientationOnlySubGimbalAxesMatchStereoDirections() {
  using namespace vrclient::adapters::unreal;
  constexpr double kAngle = 12.0;
  constexpr UnrealRotationMatrix3d kAxisPermutation{{
      0.0, 0.0, 1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  struct DirectionCase {
    const char* label;
    VrRuntimePose current;
    UnrealRotationMatrix3d expected_delta;
  };
  const std::array<DirectionCase, 3> cases{{
      {"OpenXR +X -> Unreal +pitch",
       openXrAxisRotationFixture(1.0, 0.0, 0.0, kAngle),
       unrealPitchFixture(kAngle)},
      {"OpenXR +Y -> Unreal -yaw",
       openXrAxisRotationFixture(0.0, 1.0, 0.0, kAngle),
       unrealYawFixture(-kAngle)},
      {"OpenXR +Z -> Unreal -roll",
       openXrAxisRotationFixture(0.0, 0.0, 1.0, kAngle),
       unrealRollFixture(-kAngle)},
  }};
  VrRuntimePose reference{};
  reference.orientation.w = 1.0F;
  reference.orientation_valid = 1;

  for (std::size_t index = 0; index < cases.size(); ++index) {
    UnrealOrientationPoseSample sample{};
    expect(
        makeUnrealOrientationPoseSample(
            reference, cases[index].current, index + 20, 1'000, &sample) ==
            UnrealOrientationPoseResult::Ready,
        std::string(cases[index].label) + " sample must be accepted");
    UnrealRotationMatrix3d applied{};
    expect(
        applyUnrealOrientationOnlyPose(
            kAxisPermutation, sample, 1'050, 100, &applied) ==
            UnrealOrientationPoseResult::Ready,
        std::string(cases[index].label) + " must apply to the pinned view");
    const UnrealRotationMatrix3d expected_view = multiplyOrientationFixture(
        cases[index].expected_delta, kAxisPermutation);
    expect(
        orientationMatricesNear(applied, expected_view),
        std::string(cases[index].label) +
            " must match the stereo contract's full rotation matrix");
  }
}

void testOrientationOnlyPostComposesHeadsetYawAfterBasePitch() {
  using namespace vrclient::adapters::unreal;
  constexpr UnrealRotationMatrix3d kAxisPermutation{{
      0.0, 0.0, 1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  constexpr double kRadiansPerDegree =
      3.14159265358979323846 / 180.0;

  const UnrealRotationMatrix3d base_rotation = unrealPitchFixture(30.0);
  const UnrealRotationMatrix3d current_view =
      multiplyOrientationFixture(base_rotation, kAxisPermutation);
  VrRuntimePose reference{};
  reference.orientation.w = 1.0F;
  reference.orientation_valid = 1;
  VrRuntimePose headset_yaw = reference;
  const double half_angle = 30.0 * kRadiansPerDegree * 0.5;
  headset_yaw.orientation.y = static_cast<float>(std::sin(half_angle));
  headset_yaw.orientation.w = static_cast<float>(std::cos(half_angle));
  UnrealOrientationPoseSample pose{};
  expect(
      makeUnrealOrientationPoseSample(
          reference, headset_yaw, 8, 1'000, &pose) ==
          UnrealOrientationPoseResult::Ready,
      "positive headset yaw fixture must produce an orientation sample");

  UnrealRotationMatrix3d output{};
  expect(
      applyUnrealOrientationOnlyPose(
          current_view, pose, 1'050, 100, &output) ==
          UnrealOrientationPoseResult::Ready,
      "fresh headset yaw must apply over a pitched base camera");
  const UnrealRotationMatrix3d expected_view = multiplyOrientationFixture(
      multiplyOrientationFixture(base_rotation, pose.unreal_delta),
      kAxisPermutation);
  const UnrealRotationMatrix3d world_order_wrong =
      multiplyOrientationFixture(pose.unreal_delta, current_view);
  expect(
      !orientationMatricesNear(world_order_wrong, expected_view),
      "pitched-base fixture must distinguish local and world composition");
  expect(
      orientationMatricesNear(output, expected_view),
      "orientation-only view must preserve R * D order under axis permutation");
}

void testOrientationOnlyPostComposesHeadsetPitchAfterBaseYaw() {
  using namespace vrclient::adapters::unreal;
  constexpr UnrealRotationMatrix3d kAxisPermutation{{
      0.0, 0.0, 1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  const UnrealRotationMatrix3d base_rotation = unrealYawFixture(-25.0);
  const UnrealRotationMatrix3d current_view =
      multiplyOrientationFixture(base_rotation, kAxisPermutation);
  VrRuntimePose reference{};
  reference.orientation.w = 1.0F;
  reference.orientation_valid = 1;
  const VrRuntimePose headset_pitch =
      openXrAxisRotationFixture(1.0, 0.0, 0.0, 18.0);
  UnrealOrientationPoseSample pose{};
  expect(
      makeUnrealOrientationPoseSample(
          reference, headset_pitch, 9, 1'000, &pose) ==
          UnrealOrientationPoseResult::Ready,
      "positive headset pitch fixture must produce an orientation sample");

  UnrealRotationMatrix3d output{};
  expect(
      applyUnrealOrientationOnlyPose(
          current_view, pose, 1'050, 100, &output) ==
          UnrealOrientationPoseResult::Ready,
      "fresh headset pitch must apply over a yawed base camera");
  const UnrealRotationMatrix3d expected_view = multiplyOrientationFixture(
      multiplyOrientationFixture(base_rotation, pose.unreal_delta),
      kAxisPermutation);
  const UnrealRotationMatrix3d world_order_wrong =
      multiplyOrientationFixture(pose.unreal_delta, current_view);
  expect(
      !orientationMatricesNear(world_order_wrong, expected_view),
      "yawed-base fixture must distinguish local and world composition");
  expect(
      orientationMatricesNear(output, expected_view),
      "orientation-only pitch must preserve R * D order over a yawed base");
}

void testOrientationOnlyAcceptsRuntimeFrameZero() {
  using namespace vrclient::adapters::unreal;

  VrRuntimePose pose{};
  pose.orientation.w = 1.0F;
  pose.orientation_valid = 1;
  UnrealOrientationPoseSample sample{};
  expect(
      makeUnrealOrientationPoseSample(pose, pose, 0, 1'000, &sample) ==
              UnrealOrientationPoseResult::Ready &&
          sample.valid && sample.frame_index == 0,
      "orientation-only policy must accept the OpenXR runtime's first frame");
}

void testOrientationOnlyPoseFailsClosed() {
  using namespace vrclient::adapters::unreal;

  UnrealRotationMatrix3d view{{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  }};
  UnrealRotationMatrix3d output{{
      9.0, 9.0, 9.0,
      9.0, 9.0, 9.0,
      9.0, 9.0, 9.0,
  }};
  const auto unchanged = output;
  UnrealOrientationPoseSample missing{};
  expect(
      applyUnrealOrientationOnlyPose(view, missing, 2'000, 100, &output) ==
              UnrealOrientationPoseResult::MissingOrientation &&
          output.values == unchanged.values,
      "missing orientation must leave the game view untouched");

  VrRuntimePose runtime_pose{};
  runtime_pose.orientation.w = 1.0F;
  runtime_pose.orientation_valid = 1;
  UnrealOrientationPoseSample stale{};
  expect(
      makeUnrealOrientationPoseSample(
          runtime_pose, runtime_pose, 1, 1'000, &stale) ==
          UnrealOrientationPoseResult::Ready,
      "valid pose fixture should be created");
  expect(
      applyUnrealOrientationOnlyPose(view, stale, 1'101, 100, &output) ==
              UnrealOrientationPoseResult::StalePose &&
          output.values == unchanged.values,
      "stale orientation must leave the game view untouched");
}

void testOrientationOnlySceneViewWriteTouchesRotationOnly() {
  using namespace vrclient::adapters::unreal;

  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> options{};
  const auto putDouble = [&](std::size_t offset, double value) {
    std::memcpy(options.data() + offset, &value, sizeof(value));
  };
  const auto putU64 = [&](std::size_t offset, std::uint64_t value) {
    std::memcpy(options.data() + offset, &value, sizeof(value));
  };
  putDouble(0x00, 100.0);
  putDouble(0x08, -25.0);
  putDouble(0x10, 175.0);
  const UnrealRotationMatrix3d unreal_zero_view{{
      0.0, 0.0, 1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  for (std::size_t row = 0; row < 3; ++row) {
    std::memcpy(
        options.data() + 0x20 + row * 4 * sizeof(double),
        unreal_zero_view.values.data() + row * 3,
        3 * sizeof(double));
  }
  putDouble(0xA0 + 0 * 8, 0.5625);
  putDouble(0xA0 + 5 * 8, 1.0);
  putDouble(0xA0 + 10 * 8, 10.0 / (10.0 - 1000.0));
  putDouble(0xA0 + 14 * 8, -1000.0 * 10.0 / (10.0 - 1000.0));
  putU64(0x158, 0x0000007E12345000ULL);
  putU64(0x160, 0x000001BC12345000ULL);
  const auto original = options;

  VrRuntimePose reference{};
  reference.orientation.w = 1.0F;
  reference.orientation_valid = 1;
  VrRuntimePose rotated = reference;
  constexpr float kSqrtHalf = 0.70710678F;
  rotated.orientation = {0.0F, kSqrtHalf, 0.0F, kSqrtHalf};
  rotated.position = {
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::quiet_NaN(),
  };
  rotated.position_valid = 0;
  UnrealOrientationPoseSample pose{};
  expect(
      makeUnrealOrientationPoseSample(reference, rotated, 3, 1'000, &pose) ==
          UnrealOrientationPoseResult::Ready,
      "orientation-only policy must not require a valid position");
  expect(
      applyMeccha24508135OrientationOnlyToSceneViewInitOptions(
          options.data(), options.size(), pose, 1'050, 100) ==
          UnrealOrientationPoseResult::Ready,
      "valid fresh orientation must update the Meccha scene-view options");

  bool rotation_changed = false;
  for (std::size_t index = 0; index < options.size(); ++index) {
    const std::size_t matrix_offset = index >= 0x20 ? index - 0x20 : options.size();
    const bool rotation_byte = matrix_offset < 12 * sizeof(double) &&
        (matrix_offset % (4 * sizeof(double))) < 3 * sizeof(double);
    if (rotation_byte) {
      rotation_changed = rotation_changed || options[index] != original[index];
    } else {
      expect(
          options[index] == original[index],
          "orientation-only write must not change non-rotation scene-view bytes");
    }
  }
  expect(rotation_changed, "non-neutral orientation must change the view rotation");

  UnrealCameraSample decoded{};
  const auto decode_result = decodeMeccha24508135SceneViewInitOptions(
      options.data(), options.size(), 1, 1'050, &decoded);
  UnrealRotationMatrix3d written_view{};
  for (std::size_t row = 0; row < 3; ++row) {
    std::memcpy(
        written_view.values.data() + row * 3,
        options.data() + 0x20 + row * 4 * sizeof(double),
        3 * sizeof(double));
  }
  expect(
      decode_result == UnrealSceneViewDecodeResult::Ready &&
          std::abs(decoded.rotation_degrees.x) < 0.001 &&
          std::abs(decoded.rotation_degrees.y - (-90.0)) < 0.001 &&
          std::abs(decoded.rotation_degrees.z) < 0.001,
      "OpenXR positive-Y yaw must preserve the established Unreal negative-yaw direction in the pinned Meccha view; result=" +
          std::to_string(static_cast<int>(decode_result)) + " got=" +
          std::to_string(decoded.rotation_degrees.x) + "," +
          std::to_string(decoded.rotation_degrees.y) + "," +
          std::to_string(decoded.rotation_degrees.z) + " view=" +
          std::to_string(written_view.values[0]) + "," +
          std::to_string(written_view.values[1]) + "," +
          std::to_string(written_view.values[2]) + ";" +
          std::to_string(written_view.values[3]) + "," +
          std::to_string(written_view.values[4]) + "," +
          std::to_string(written_view.values[5]) + ";" +
          std::to_string(written_view.values[6]) + "," +
          std::to_string(written_view.values[7]) + "," +
          std::to_string(written_view.values[8]));
}

void testStableCameraObservation() {
  using namespace vrclient::adapters::unreal;

  UnrealCameraSample camera{};
  camera.camera_id = 7;
  camera.sample_time_ns = 1'000;
  camera.active = true;
  camera.vertical_fov_degrees = 80.0F;
  camera.aspect_ratio = 16.0F / 9.0F;
  camera.near_clip_uu = 10.0F;
  camera.far_clip_uu = 100'000.0F;

  UnrealCameraObserver observer(3, 100);
  auto other = camera;
  other.camera_id = 8;
  expect(
      observer.observe({camera, other}, 1'000) ==
          UnrealCameraObservationResult::Ambiguous,
      "multiple valid active cameras must fail closed");

  for (std::uint64_t sample_index = 1; sample_index <= 3; ++sample_index) {
    camera.sample_index = sample_index;
    camera.sample_time_ns = 1'000 + static_cast<std::int64_t>(sample_index);
    const auto result = observer.observe({camera}, camera.sample_time_ns);
    expect(
        result == (sample_index < 3
                       ? UnrealCameraObservationResult::Pending
                       : UnrealCameraObservationResult::Ready),
        "camera must remain stable for the configured sample count");
  }
  const auto stable = observer.snapshot();
  expect(
      stable.ready && stable.sample.camera_id == 7 &&
          stable.sample.sample_index == 3,
      "stable camera snapshot must preserve exact identity and latest sample");
  expect(
      observer.observe({camera}, camera.sample_time_ns) ==
          UnrealCameraObservationResult::NonMonotonic,
      "repeated camera sample index must fail closed");
}

void testOrientationIdentityGateRejectsRecentCompetitor() {
  using namespace vrclient::adapters::unreal;

  MecchaCameraIdentityGate gate(3, 100, 100);
  expect(
      gate.observe(7, 1'000) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'010) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'020) == MecchaCameraIdentityResult::Ready,
      "one recent identity should become stable after the configured samples");
  expect(gate.allows(7, 1'020), "the unique fresh stable identity should write");
  expect(
      gate.observe(8, 1'021) == MecchaCameraIdentityResult::Ambiguous &&
          gate.stableCameraId() == 0 && !gate.allows(7, 1'021) &&
          !gate.allows(8, 1'021),
      "a competing recent identity must disable all writes immediately");

  expect(
      gate.observe(7, 1'122) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'132) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'142) == MecchaCameraIdentityResult::Ready,
      "stability must be re-earned after competing identities leave the window");
  expect(
      !gate.allows(7, 1'243) &&
          gate.observe(7, 1'243) == MecchaCameraIdentityResult::Pending &&
          gate.stableCameraId() == 0,
      "a stale identity must stop writing and restart stability after an observation gap");
}

void testStereoIdentityGateRequiresExactlyOneStablePair() {
  using namespace vrclient::adapters::unreal;

  MecchaStereoCameraIdentityGate gate(3, 100, 100);
  expect(
      gate.observe(7, 1'000) == MecchaCameraIdentityResult::Pending &&
          gate.observe(8, 1'001) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'010) == MecchaCameraIdentityResult::Pending &&
          gate.observe(8, 1'011) == MecchaCameraIdentityResult::Pending &&
          gate.observe(7, 1'020) == MecchaCameraIdentityResult::Pending &&
          gate.observe(8, 1'021) == MecchaCameraIdentityResult::Ready,
      "alternating left/right identities must earn readiness as one pair");
  expect(
      gate.allows(7, 1'021) && gate.allows(8, 1'021) &&
          gate.stableCameraIds() == std::array<std::uint64_t, 2>{7, 8},
      "both and only both stable eye identities must be writable");
  expect(
      gate.observe(9, 1'022) == MecchaCameraIdentityResult::Ambiguous &&
          !gate.allows(7, 1'022) && !gate.allows(8, 1'022),
      "a third recent identity must immediately disable stereo writes");
}

void testStereoPosePairUsesOnePoseGenerationForBothEyes() {
  using namespace vrclient::adapters::unreal;

  MecchaStereoPosePairCoordinator coordinator;
  const std::array<std::uint64_t, 2> eyes{7, 8};
  UnrealOrientationPoseSample first_pose{};
  first_pose.valid = true;
  first_pose.frame_index = 41;
  first_pose.publish_time_ns = 990;
  first_pose.unreal_delta.values[0] = 1.0;
  UnrealOrientationPoseSample newer_pose = first_pose;
  newer_pose.frame_index = 42;
  newer_pose.publish_time_ns = 995;
  newer_pose.unreal_delta.values[0] = 2.0;
  MecchaStereoPosePairSelection first{};
  MecchaStereoPosePairSelection second{};

  expect(
      coordinator.select(
          7, eyes, 100, {1.0, 2.0, 3.0}, 1'000, first_pose, true,
          &first) ==
              MecchaStereoPosePairResult::ReadyFirstEye &&
          coordinator.select(
              8, eyes, 100, {1.0, 8.0, 3.0}, 1'010, newer_pose, false,
              &second) ==
              MecchaStereoPosePairResult::ReadyPairComplete,
      "one stereo pair should accept both distinct stable eye identities");
  expect(
      first.pose.frame_index == 41 && second.pose.frame_index == 41 &&
          first.pose.unreal_delta.values == second.pose.unreal_delta.values &&
          second.completed_pair_count == 1 &&
          second.pose_frame_mismatch_count == 0 &&
          second.eye_camera_ids == eyes &&
          second.render_generation == 100 &&
          second.write_enabled && second.pose_validation_time_ns == 1'000 &&
          second.eye_locations_uu[0].y == 2.0 &&
          second.eye_locations_uu[1].y == 8.0 &&
          second.eye_separation_uu == 6.0 &&
          second.eye_observation_delta_ns == 10,
      "the second eye must reuse the first eye's latched pose and report coherent pair measurements");
}

void testStereoPosePairKeepsDuplicatesCoherentAndCountsIncompleteFrames() {
  using namespace vrclient::adapters::unreal;

  MecchaStereoPosePairCoordinator coordinator;
  const std::array<std::uint64_t, 2> eyes{7, 8};
  UnrealOrientationPoseSample pose{};
  pose.valid = true;
  pose.frame_index = 10;
  pose.publish_time_ns = 990;
  MecchaStereoPosePairSelection selected{};

  expect(
      coordinator.select(7, eyes, 100, {}, 1'000, pose, true, &selected) ==
              MecchaStereoPosePairResult::ReadyFirstEye &&
          coordinator.select(7, eyes, 100, {}, 1'001, pose, false, &selected) ==
              MecchaStereoPosePairResult::ReadyDuplicateEye &&
          selected.pose.frame_index == pose.frame_index &&
          selected.write_enabled && selected.pose_validation_time_ns == 1'000,
      "a repeated eye in one render generation must reuse the latched pose without advancing the pair");
  UnrealOrientationPoseSample next_pose = pose;
  next_pose.frame_index = 11;
  next_pose.publish_time_ns = 1'090;
  expect(
      coordinator.select(
          8, eyes, 101, {}, 1'101, next_pose, false, &selected) ==
              MecchaStereoPosePairResult::ReadyFirstEye &&
          selected.pose.frame_index == 11 && !selected.write_enabled &&
          coordinator.incompletePairCount() == 1,
      "a new Present generation must count the abandoned pair and latch a fresh pose for the new frame");
  expect(
      coordinator.select(9, eyes, 101, {}, 1'102, pose, true, &selected) ==
          MecchaStereoPosePairResult::InvalidIdentity,
      "a third identity must never receive the latched stereo pose");
}

void testStereoProjectionGateRequiresConfiguredEngineFov() {
  using namespace vrclient::adapters::unreal;
  expect(
      mecchaStereoProjectionMatches(
          110.0F, 1.0F, 110.0F, 1.0F, 2.0F, 0.01F) &&
          mecchaStereoProjectionMatches(
              108.25F, 1.005F, 110.0F, 1.0F, 2.0F, 0.01F),
      "stereo projection gate should allow bounded FOV/aspect tolerance");
  expect(
      !mecchaStereoProjectionMatches(
          58.7F, 1.0F, 110.0F, 1.0F, 2.0F, 0.01F) &&
          !mecchaStereoProjectionMatches(
              110.0F, 1.0468F, 110.0F, 1.0F, 2.0F, 0.01F) &&
          !mecchaStereoProjectionMatches(
              112.1F, 1.0F, 110.0F, 1.0F, 2.0F, 0.01F) &&
          !mecchaStereoProjectionMatches(
              std::numeric_limits<float>::quiet_NaN(), 1.0F,
              110.0F, 1.0F, 2.0F, 0.01F),
      "ignored, skewed, mismatched, or non-finite projection must fail closed");
}

void testMecchaStereoProjectionNormalizationTouchesOnlyAngularScales() {
  using namespace vrclient::adapters::unreal;

  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> options{};
  const auto putDouble = [&](std::size_t offset, double value) {
    std::memcpy(options.data() + offset, &value, sizeof(value));
  };
  const auto putU64 = [&](std::size_t offset, std::uint64_t value) {
    std::memcpy(options.data() + offset, &value, sizeof(value));
  };
  const auto source_scale_y = 1.0 / std::tan(
      kMeccha24517175StockStereoVerticalFovDegrees *
      3.14159265358979323846 / 360.0);
  const auto source_scale_x = source_scale_y /
      kMeccha24517175StockStereoAspectRatio;
  const double axis_swap[16] = {
      0, 0, 1, 0,
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 0, 1};
  std::memcpy(options.data() + 0x20, axis_swap, sizeof(axis_swap));
  putDouble(0xA0 + 0 * sizeof(double), source_scale_x);
  putDouble(0xA0 + 5 * sizeof(double), source_scale_y);
  putDouble(0xA0 + 10 * sizeof(double), 10.0 / (10.0 - 1000.0));
  putDouble(
      0xA0 + 14 * sizeof(double),
      -1000.0 * 10.0 / (10.0 - 1000.0));
  putU64(0x158, 0x0000007E12345000ULL);
  putU64(0x160, 0x000001BC12345000ULL);
  const auto original = options;

  expect(
      normalizeMeccha24517175StereoProjection(
          options.data(), options.size()) ==
          MecchaStereoProjectionNormalizeResult::Ready,
      "the exact stock Meccha fake-stereo projection should normalize");

  bool scale_x_changed = false;
  bool scale_y_changed = false;
  for (std::size_t index = 0; index < options.size(); ++index) {
    const bool scale_x_byte = index >= 0xA0 && index < 0xA8;
    const bool scale_y_byte = index >= 0xC8 && index < 0xD0;
    if (scale_x_byte) {
      scale_x_changed = scale_x_changed || options[index] != original[index];
    } else if (scale_y_byte) {
      scale_y_changed = scale_y_changed || options[index] != original[index];
    } else {
      expect(
          options[index] == original[index],
          "projection normalization must preserve every non-angular byte");
    }
  }
  expect(
      scale_x_changed && scale_y_changed,
      "projection normalization must change both angular scales");

  UnrealCameraSample decoded{};
  expect(
      decodeMeccha24508135SceneViewInitOptions(
          options.data(), options.size(), 1, 1, &decoded) ==
              UnrealSceneViewDecodeResult::Ready &&
          std::abs(decoded.vertical_fov_degrees - 110.0F) < 0.001F &&
          std::abs(decoded.aspect_ratio - 1.0F) < 0.001F,
      "normalized scene-view options must decode as symmetric 110-degree stereo");

  const auto already_normalized = options;
  expect(
      normalizeMeccha24517175StereoProjection(
          options.data(), options.size()) ==
              MecchaStereoProjectionNormalizeResult::AlreadyNormalized &&
          options == already_normalized,
      "an already-correct projection must be accepted without another write");

  auto unexpected = original;
  const double flat_scale_y = 1.0 / std::tan(58.7 * 3.14159265358979323846 / 360.0);
  const double flat_scale_x = flat_scale_y / (16.0 / 9.0);
  std::memcpy(unexpected.data() + 0xA0, &flat_scale_x, sizeof(flat_scale_x));
  std::memcpy(unexpected.data() + 0xC8, &flat_scale_y, sizeof(flat_scale_y));
  const auto unexpected_original = unexpected;
  expect(
      normalizeMeccha24517175StereoProjection(
          unexpected.data(), unexpected.size()) ==
              MecchaStereoProjectionNormalizeResult::UnexpectedSourceProjection &&
          unexpected == unexpected_original,
      "flat or unknown projections must fail closed without writes");

  auto non_finite = original;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::memcpy(non_finite.data() + 0xA0, &nan, sizeof(nan));
  const auto non_finite_original = non_finite;
  expect(
      normalizeMeccha24517175StereoProjection(
          non_finite.data(), non_finite.size()) ==
              MecchaStereoProjectionNormalizeResult::NonFiniteProjection &&
          non_finite == non_finite_original &&
          normalizeMeccha24517175StereoProjection(nullptr, options.size()) ==
              MecchaStereoProjectionNormalizeResult::InvalidInput &&
          normalizeMeccha24517175StereoProjection(
              options.data(), 0xCF) ==
              MecchaStereoProjectionNormalizeResult::InvalidInput,
      "invalid projection inputs must fail closed without writes");
}

void testMecchaCameraPinTargetsBuild24517175() {
  using namespace vrclient::adapters::unreal;
  const auto& seam = mecchaCameraViewSeam();
  const std::vector<std::uint8_t> expected_prologue{
      0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x57, 0x48, 0x81,
      0xEC, 0x30, 0x04, 0x00, 0x00, 0x48, 0x8B, 0x05,
      0x9C, 0xB0, 0x6B, 0x05, 0x48, 0x33, 0xC4, 0x48,
      0x89, 0x84, 0x24, 0x60, 0x03, 0x00, 0x00, 0x49,
      0x89, 0x6B, 0x18, 0x48, 0x8D, 0x05, 0x3E, 0xD9,
      0x9A, 0x03, 0x48, 0x89, 0x01, 0x33, 0xED, 0x48,
      0x8B, 0x82, 0x58, 0x01, 0x00, 0x00, 0x48, 0x8B,
      0xF2, 0x48, 0x89, 0x41, 0x08, 0x48, 0x8B, 0xD9,
  };
  expect(
      mecchaCameraBuildId() == "steam-4704690-build-24517175" &&
          mecchaCameraExecutableSha256() ==
              "2192bea467070e9ac051d587a5b2d3afd9fd525580692c361db67a371f0883ce" &&
          seam.constructor_rva == 0x041A9610 &&
          seam.prologue == expected_prologue &&
          seam.expected_direct_call_references == 12 &&
          seam.primary_anchor_window_bytes == 0x2000,
      "Meccha camera pin must match the independently revalidated 24517175 seam");
}

void testOrientationTrackingGateRewarmsAfterTransientFreshnessLoss() {
  using namespace vrclient::adapters::unreal;

  MecchaOrientationTrackingGate gate(3, 100);
  expect(
      gate.observePose(true, 1'000) ==
              MecchaOrientationTrackingResult::Warming &&
          gate.observePose(true, 1'010) ==
              MecchaOrientationTrackingResult::Warming &&
          !gate.allowsWrite(1'010),
      "orientation writes must remain disabled until pose freshness is proven");
  expect(
      gate.observePose(true, 1'020) ==
              MecchaOrientationTrackingResult::Ready &&
          gate.allowsWrite(1'020),
      "a continuous fresh pose stream should enable orientation writes");
  expect(
      !gate.allowsWrite(1'121) && !gate.ready() && !gate.faultLatched(),
      "one stale pose must stop writes without converting a scheduler gap into a structural fault");
  expect(
      gate.observePose(true, 1'122) ==
              MecchaOrientationTrackingResult::Warming &&
          gate.observePose(true, 1'132) ==
              MecchaOrientationTrackingResult::Warming &&
          gate.observePose(true, 1'142) ==
              MecchaOrientationTrackingResult::Ready &&
          gate.allowsWrite(1'142) && !gate.faultLatched(),
      "fresh tracking must re-earn the complete warm-up after a transient gap");

  MecchaOrientationTrackingGate missing_gate(2, 100);
  expect(
      missing_gate.observePose(true, 2'000) ==
              MecchaOrientationTrackingResult::Warming &&
      missing_gate.observePose(true, 2'010) ==
              MecchaOrientationTrackingResult::Ready &&
      missing_gate.observePose(false, 2'011) ==
              MecchaOrientationTrackingResult::Warming &&
          !missing_gate.allowsWrite(2'011) && !missing_gate.faultLatched() &&
          missing_gate.observePose(true, 2'012) ==
              MecchaOrientationTrackingResult::Warming &&
          missing_gate.observePose(true, 2'022) ==
              MecchaOrientationTrackingResult::Ready &&
          missing_gate.allowsWrite(2'022),
      "missing tracking must stop writes and require warm-up without becoming an unrecoverable fault");
}

void testOrientationTrackingStateRewarmsAfterPublisherGap() {
  using namespace vrclient::adapters::unreal;

  MecchaOrientationTrackingState state(3, 100);
  expect(
      state.publishPose(true, 3'000) ==
              MecchaOrientationTrackingResult::Warming &&
          state.publishPose(true, 3'010) ==
              MecchaOrientationTrackingResult::Warming &&
          state.publishPose(true, 3'020) ==
              MecchaOrientationTrackingResult::Ready &&
          state.ready(),
      "tracking state fixture must become ready after the full warm-up");
  expect(
      state.publishPose(true, 3'121) ==
              MecchaOrientationTrackingResult::Warming &&
          !state.ready() && !state.faultLatched() &&
          state.publishPose(true, 3'131) ==
              MecchaOrientationTrackingResult::Warming &&
          state.publishPose(true, 3'141) ==
              MecchaOrientationTrackingResult::Warming &&
          state.publishPose(true, 3'151) ==
              MecchaOrientationTrackingResult::Ready &&
          state.ready() && !state.faultLatched(),
      "publisher scheduling gaps must rewarm instead of permanently disabling a healthy stream");
}

void testOrientationTrackingStateKeepsConcurrentFaultLatched() {
  using namespace vrclient::adapters::unreal;

  struct PublishBarrier {
    std::atomic<bool> armed{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
  } barrier;
  const auto pauseBeforeReadyPublish = [](void* user_data) {
    auto* barrier = static_cast<PublishBarrier*>(user_data);
    if (!barrier->armed.load(std::memory_order_acquire)) {
      return;
    }
    barrier->entered.store(true, std::memory_order_release);
    while (!barrier->release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  };
  MecchaOrientationTrackingState state(
      1, 100, pauseBeforeReadyPublish, &barrier);
  expect(
      state.publishPose(true, 1'000) ==
              MecchaOrientationTrackingResult::Ready &&
          state.ready() && !state.faultLatched(),
      "one valid pose should ready a one-sample tracking state");

  barrier.armed.store(true, std::memory_order_release);
  auto publisher_result = MecchaOrientationTrackingResult::Warming;
  std::thread publisher([&]() {
    publisher_result = state.publishPose(true, 1'001);
  });
  while (!barrier.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  state.latchFault();
  barrier.release.store(true, std::memory_order_release);
  publisher.join();

  expect(
      publisher_result == MecchaOrientationTrackingResult::FaultLatched &&
          state.faultLatched() && !state.ready() &&
          state.publishPose(true, 1'002) ==
              MecchaOrientationTrackingResult::FaultLatched,
      "a fault latched inside the publisher critical interval must override ready publication");
}

void testOrientationTelemetrySnapshotNeverMixesGenerations() {
  using namespace vrclient::adapters::unreal;
  struct PublishBarrier {
    std::atomic<bool> armed{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
  } barrier;
  const auto pauseMidPublication = [](void* user_data) {
    auto* barrier = static_cast<PublishBarrier*>(user_data);
    if (!barrier->armed.load(std::memory_order_acquire)) {
      return;
    }
    barrier->entered.store(true, std::memory_order_release);
    while (!barrier->release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  };
  MecchaOrientationTelemetryBuffer telemetry(pauseMidPublication, &barrier);
  MecchaOrientationTelemetrySample first{};
  first.pose.valid = true;
  first.pose.frame_index = 10;
  first.pose.publish_time_ns = 100;
  first.pose_publish_count = 10;
  first.pose.unreal_delta.values = {{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  }};
  first.sampler_active = true;
  first.sampler_result = VR_RUNTIME_OK;
  first.sampler_attempt_count = 10;
  first.sampler_success_count = 9;
  telemetry.publish(first);

  MecchaOrientationTelemetrySample second = first;
  second.pose.frame_index = 11;
  second.pose.publish_time_ns = 200;
  second.pose_publish_count = 11;
  second.pose.unreal_delta.values = {{
      0.0, -1.0, 0.0,
      1.0, 0.0, 0.0,
      0.0, 0.0, 1.0,
  }};
  second.sampler_attempt_count = 11;
  second.sampler_success_count = 10;
  barrier.armed.store(true, std::memory_order_release);
  std::thread publisher([&]() { telemetry.publish(second); });
  while (!barrier.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  MecchaOrientationTelemetrySample unavailable = first;
  const bool read_during_publication = telemetry.read(&unavailable);
  barrier.release.store(true, std::memory_order_release);
  publisher.join();
  MecchaOrientationTelemetrySample published{};
  const bool read_after_publication = telemetry.read(&published);

  expect(
      !read_during_publication &&
          unavailable.pose.frame_index == first.pose.frame_index &&
          unavailable.sampler_attempt_count == first.sampler_attempt_count,
      "bounded telemetry read must not expose a mid-publication generation");
  expect(
      read_after_publication && published.pose.valid &&
          published.pose.frame_index == second.pose.frame_index &&
          published.pose.publish_time_ns == second.pose.publish_time_ns &&
          published.pose_publish_count == second.pose_publish_count &&
          published.pose.unreal_delta.values ==
              second.pose.unreal_delta.values &&
          published.sampler_result == second.sampler_result &&
          published.sampler_attempt_count ==
              second.sampler_attempt_count &&
          published.sampler_success_count ==
              second.sampler_success_count &&
          published.sampler_success_count <=
              published.sampler_attempt_count,
      "coherent telemetry must publish one complete pose and sampler generation");
}

void testOrientationOperationalSnapshotRejectsMixedCounterGenerations() {
  using namespace vrclient::adapters::unreal;
  struct OperationalCounters {
    std::atomic<std::uint64_t> constructor_hits{3};
    std::atomic<std::uint64_t> decoded_cameras{2};
    std::atomic<std::uint64_t> rejected_cameras{1};
    std::atomic<std::uint64_t> stable_camera_id{42};
    std::atomic<std::uint64_t> secondary_stable_camera_id{43};
    std::atomic<std::uint64_t> orientation_applies{2};
    std::atomic<std::uint64_t> consecutive_applies{2};
    std::atomic<std::uint64_t> missing_pose_skips{0};
    std::atomic<std::uint64_t> stale_pose_skips{0};
    std::atomic<std::uint64_t> write_rejects{0};
    std::atomic<bool> tracking_ready{true};
    std::atomic<bool> tracking_fault_latched{false};
    std::atomic<bool> mutate_application{true};
    std::atomic<int> valid_attempts{0};
  } counters;
  const auto loadTrackingReady = [](void* user_data) {
    return static_cast<OperationalCounters*>(user_data)
        ->tracking_ready.load(std::memory_order_acquire);
  };
  const auto loadTrackingFault = [](void* user_data) {
    return static_cast<OperationalCounters*>(user_data)
        ->tracking_fault_latched.load(std::memory_order_acquire);
  };
  const auto mutateBetweenCollections = [](
      MecchaOrientationOperationalReadStage stage, void* user_data) {
    auto* counters = static_cast<OperationalCounters*>(user_data);
    counters->valid_attempts.fetch_add(1, std::memory_order_relaxed);
    if (stage == MecchaOrientationOperationalReadStage::WholeTupleCollected &&
        counters->mutate_application.exchange(
            false, std::memory_order_acq_rel)) {
      counters->constructor_hits.fetch_add(1, std::memory_order_release);
      counters->decoded_cameras.fetch_add(1, std::memory_order_release);
      counters->stable_camera_id.store(84, std::memory_order_release);
      counters->secondary_stable_camera_id.store(85, std::memory_order_release);
      counters->orientation_applies.fetch_add(1, std::memory_order_release);
      counters->consecutive_applies.fetch_add(1, std::memory_order_release);
      counters->tracking_fault_latched.store(true, std::memory_order_release);
      counters->tracking_ready.store(false, std::memory_order_release);
    }
  };
  MecchaOrientationOperationalSources sources{};
  sources.constructor_hits = &counters.constructor_hits;
  sources.decoded_cameras = &counters.decoded_cameras;
  sources.rejected_cameras = &counters.rejected_cameras;
  sources.stable_camera_id = &counters.stable_camera_id;
  sources.secondary_stable_camera_id = &counters.secondary_stable_camera_id;
  sources.orientation_applies = &counters.orientation_applies;
  sources.consecutive_applies = &counters.consecutive_applies;
  sources.missing_pose_skips = &counters.missing_pose_skips;
  sources.stale_pose_skips = &counters.stale_pose_skips;
  sources.write_rejects = &counters.write_rejects;
  sources.load_tracking_ready = loadTrackingReady;
  sources.load_tracking_fault = loadTrackingFault;
  sources.tracking_user_data = &counters;

  MecchaOrientationOperationalSnapshot snapshot{};
  const bool collected = collectMecchaOrientationOperationalSnapshot(
      sources, &snapshot, mutateBetweenCollections, &counters);

  expect(
      collected &&
          counters.valid_attempts.load(std::memory_order_relaxed) == 2 &&
          snapshot.constructor_hit_count == 4 &&
           snapshot.decoded_camera_count == 3 &&
           snapshot.rejected_camera_count == 1 &&
           snapshot.stable_camera_id == 84 &&
           snapshot.secondary_stable_camera_id == 85 &&
           snapshot.decoded_camera_count + snapshot.rejected_camera_count <=
               snapshot.constructor_hit_count,
      "constructor and terminal counters must come from one complete generation");
  expect(
      collected && snapshot.orientation_apply_count == 3 &&
           snapshot.consecutive_orientation_apply_count == 3 &&
           snapshot.consecutive_orientation_apply_count <=
               snapshot.orientation_apply_count &&
           snapshot.orientation_apply_count +
                   snapshot.missing_pose_skip_count +
                   snapshot.stale_pose_skip_count +
                   snapshot.write_reject_count <=
               snapshot.decoded_camera_count,
      "terminal outcomes must not outnumber decoded cameras");
  expect(
      collected && snapshot.tracking_fault_latched &&
          !snapshot.tracking_ready,
      "tracking readiness and the fault latch must come from one stable generation");

  counters.constructor_hits.store(3, std::memory_order_release);
  counters.decoded_cameras.store(2, std::memory_order_release);
  counters.orientation_applies.store(6, std::memory_order_release);
  counters.consecutive_applies.store(6, std::memory_order_release);
  counters.missing_pose_skips.store(2, std::memory_order_release);
  counters.stale_pose_skips.store(1, std::memory_order_release);
  counters.write_rejects.store(1, std::memory_order_release);
  std::atomic<int> exhausted_attempts{0};
  const auto countInvalidAttempts = [](
      MecchaOrientationOperationalReadStage, void* user_data) {
    static_cast<std::atomic<int>*>(user_data)->fetch_add(
        1, std::memory_order_relaxed);
  };
  MecchaOrientationOperationalSnapshot unavailable{};
  const bool invalid_collected = collectMecchaOrientationOperationalSnapshot(
      sources, &unavailable, countInvalidAttempts, &exhausted_attempts);
  expect(
      !invalid_collected &&
          exhausted_attempts.load(std::memory_order_relaxed) == 64 &&
          unavailable.constructor_hit_count == 0 &&
          unavailable.decoded_camera_count == 0 &&
          unavailable.rejected_camera_count == 0 &&
          unavailable.stable_camera_id == 0 &&
          unavailable.secondary_stable_camera_id == 0 &&
          unavailable.orientation_apply_count == 0 &&
          unavailable.consecutive_orientation_apply_count == 0 &&
          unavailable.missing_pose_skip_count == 0 &&
          unavailable.stale_pose_skip_count == 0 &&
          unavailable.write_reject_count == 0 &&
          !unavailable.tracking_ready &&
          !unavailable.tracking_fault_latched,
      "an impossible tuple must exhaust bounded reads and preserve defaults");
}

void testFSceneViewLocator() {
  using namespace vrclient::adapters::unreal;

  UnrealViewSeamImage image{};
  image.text.rva = 0x1000;
  image.text.bytes.assign(0x100, 0x90);
  image.rdata.rva = 0x2000;
  const std::u16string primary = u"r.TranslucentSortPolicy";
  const std::u16string secondary = u"vr.InstancedStereo";
  const auto appendWide = [&](const std::u16string& value) {
    const std::uint32_t rva = image.rdata.rva +
        static_cast<std::uint32_t>(image.rdata.bytes.size());
    for (const char16_t character : value) {
      image.rdata.bytes.push_back(static_cast<std::uint8_t>(character));
      image.rdata.bytes.push_back(static_cast<std::uint8_t>(character >> 8));
    }
    image.rdata.bytes.push_back(0);
    image.rdata.bytes.push_back(0);
    return rva;
  };
  const std::uint32_t primary_rva = appendWide(primary);
  const std::uint32_t secondary_rva = appendWide(secondary);
  const auto writeLea = [&](std::size_t offset, std::uint32_t target_rva) {
    image.text.bytes[offset + 0] = 0x48;
    image.text.bytes[offset + 1] = 0x8D;
    image.text.bytes[offset + 2] = 0x0D;
    const std::uint32_t next_rva = image.text.rva +
        static_cast<std::uint32_t>(offset + 7);
    const std::int32_t displacement =
        static_cast<std::int32_t>(target_rva - next_rva);
    std::memcpy(image.text.bytes.data() + offset + 3,
                &displacement, sizeof(displacement));
  };
  writeLea(0x10, primary_rva);
  writeLea(0x28, secondary_rva);
  image.runtime_functions.push_back({0x1000, 0x1040});
  image.runtime_functions.push_back({0x1040, 0x1080});

  const std::string hash(64, 'a');
  const auto found = locateUnrealViewConstructor(image, hash, hash);
  expect(
      found.result == UnrealViewSeamLocateResult::Ready &&
          found.constructor_rva == 0x1000 && found.candidate_count == 1,
      "correlated renderer anchors must locate one constructor function");

  expect(
      locateUnrealViewConstructor(image, hash, std::string(64, 'b')).result ==
          UnrealViewSeamLocateResult::FingerprintMismatch,
      "a different executable fingerprint must fail closed");

  auto ambiguous = image;
  writeLea(0x50, primary_rva);
  writeLea(0x68, secondary_rva);
  ambiguous.text.bytes = image.text.bytes;
  expect(
      locateUnrealViewConstructor(ambiguous, hash, hash).result ==
          UnrealViewSeamLocateResult::AmbiguousCandidate,
      "multiple correlated functions must fail closed");

  auto call_graph = image;
  std::fill(call_graph.text.bytes.begin() + 0x28,
            call_graph.text.bytes.begin() + 0x2F, 0x90);
  const std::uint32_t call_rva = call_graph.text.rva + 0x20;
  const std::int32_t call_displacement =
      static_cast<std::int32_t>(0x1080 - (call_rva + 5));
  call_graph.text.bytes[0x20] = 0xE8;
  std::memcpy(call_graph.text.bytes.data() + 0x21,
              &call_displacement, sizeof(call_displacement));
  const auto writeGraphLea = [&](std::size_t offset, std::uint32_t target_rva) {
    call_graph.text.bytes[offset + 0] = 0x48;
    call_graph.text.bytes[offset + 1] = 0x8D;
    call_graph.text.bytes[offset + 2] = 0x0D;
    const std::int32_t displacement = static_cast<std::int32_t>(
        target_rva - (call_graph.text.rva + offset + 7));
    std::memcpy(call_graph.text.bytes.data() + offset + 3,
                &displacement, sizeof(displacement));
  };
  writeGraphLea(0x88, secondary_rva);
  call_graph.text.bytes[0x98] = 0xC3;
  call_graph.runtime_functions = {{0x1000, 0x1040}, {0x1080, 0x10A0}};
  expect(
      locateUnrealViewConstructor(call_graph, hash, hash).result ==
          UnrealViewSeamLocateResult::MissingCandidate,
      "separate anchor functions must not pass the direct locator");
  const auto exhaustive = locateUnrealViewConstructorExhaustive(
      call_graph, hash, hash, &decodeSyntheticInstruction);
  expect(
      exhaustive.result == UnrealViewSeamLocateResult::Ready &&
          exhaustive.constructor_rva == 0x1000,
      "bounded call-graph decode must correlate split renderer anchors");
}

void testPinnedFSceneViewLocator() {
  using namespace vrclient::adapters::unreal;
  UnrealViewSeamImage image{};
  image.text.rva = 0x1000;
  image.text.bytes.assign(0x180, 0x90);
  image.rdata.rva = 0x3000;
  const auto appendWide = [&](std::u16string_view value) {
    const auto rva = image.rdata.rva +
        static_cast<std::uint32_t>(image.rdata.bytes.size());
    for (char16_t character : value) {
      image.rdata.bytes.push_back(static_cast<std::uint8_t>(character));
      image.rdata.bytes.push_back(static_cast<std::uint8_t>(character >> 8));
    }
    image.rdata.bytes.push_back(0);
    image.rdata.bytes.push_back(0);
    return rva;
  };
  const auto primary = appendWide(u"r.TranslucentSortPolicy");
  const auto secondary = appendWide(u"vr.InstancedStereo");
  const std::vector<std::uint8_t> prologue = {0x4C, 0x8B, 0xDC, 0x53};
  std::copy(prologue.begin(), prologue.end(), image.text.bytes.begin() + 0x40);
  const auto writeCall = [&](std::size_t offset) {
    image.text.bytes[offset] = 0xE8;
    const auto source_next = image.text.rva +
        static_cast<std::uint32_t>(offset + 5);
    const std::int32_t displacement =
        static_cast<std::int32_t>(0x1040 - source_next);
    std::memcpy(image.text.bytes.data() + offset + 1, &displacement, 4);
  };
  writeCall(0x10);
  writeCall(0x120);
  image.text.bytes[0x80] = 0x48;
  image.text.bytes[0x81] = 0x8D;
  image.text.bytes[0x82] = 0x0D;
  const std::int32_t anchor_displacement = static_cast<std::int32_t>(
      primary - (image.text.rva + 0x87));
  std::memcpy(image.text.bytes.data() + 0x83, &anchor_displacement, 4);
  image.text.bytes[0xA0] = 0x48;
  image.text.bytes[0xA1] = 0x8D;
  image.text.bytes[0xA2] = 0x0D;
  const std::int32_t secondary_displacement = static_cast<std::int32_t>(
      secondary - (image.text.rva + 0xA7));
  std::memcpy(image.text.bytes.data() + 0xA3, &secondary_displacement, 4);
  const std::string hash(64, 'a');
  const UnrealPinnedViewSeam pin{0x1040, prologue, 2, 0x80};

  const auto ready = validatePinnedUnrealViewConstructor(image, hash, hash, pin);
  expect(ready.result == UnrealViewSeamLocateResult::Ready &&
             ready.constructor_rva == 0x1040 && ready.candidate_count == 1 &&
             ready.primary_reference_count == 1 &&
             ready.direct_call_reference_count == 2,
         "exact pinned constructor evidence must validate (result=" +
             std::to_string(static_cast<int>(ready.result)) + ", primary=" +
             std::to_string(ready.primary_reference_count) + ", secondary=" +
             std::to_string(ready.secondary_reference_count) + ", calls=" +
             std::to_string(ready.direct_call_reference_count) + ")");

  auto bad_signature = image;
  bad_signature.text.bytes[0x40] ^= 0xFF;
  expect(validatePinnedUnrealViewConstructor(
             bad_signature, hash, hash, pin).result ==
             UnrealViewSeamLocateResult::SignatureMismatch,
         "changed constructor bytes must fail closed");

  auto wrong_calls = image;
  wrong_calls.text.bytes[0x120] = 0x90;
  expect(validatePinnedUnrealViewConstructor(
             wrong_calls, hash, hash, pin).result ==
             UnrealViewSeamLocateResult::ReferenceCountMismatch,
         "changed caller topology must fail closed");
}

void testDoubleFSceneViewDecoder() {
  using namespace vrclient::adapters::unreal;
  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> raw{};
  const auto putDouble = [&](std::size_t offset, double value) {
    std::memcpy(raw.data() + offset, &value, sizeof(value));
  };
  const auto putU64 = [&](std::size_t offset, std::uint64_t value) {
    std::memcpy(raw.data() + offset, &value, sizeof(value));
  };
  const auto putI32 = [&](std::size_t offset, std::int32_t value) {
    std::memcpy(raw.data() + offset, &value, sizeof(value));
  };
  putDouble(0x00, 100.0);
  putDouble(0x08, -25.0);
  putDouble(0x10, 175.0);
  // Zero UE rotator: InverseRotation * UE axis permutation.
  const double axis_swap[16] = {
      0, 0, 1, 0,
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 0, 1};
  std::memcpy(raw.data() + 0x20, axis_swap, sizeof(axis_swap));
  putDouble(0xA0 + 0 * 8, 0.5625);
  putDouble(0xA0 + 5 * 8, 1.0);
  putDouble(0xA0 + 10 * 8, 10.0 / (10.0 - 1000.0));
  putDouble(0xA0 + 14 * 8, -1000.0 * 10.0 / (10.0 - 1000.0));
  putU64(0x158, 0x0000007E12345000ULL);
  putU64(0x160, 0x000001BC12345000ULL);

  UnrealCameraSample camera{};
  UnrealSceneViewIdentity identity{};
  expect(decodeMeccha24508135SceneViewInitOptions(
             raw.data(), raw.size(), 7, 99, &camera, &identity) ==
             UnrealSceneViewDecodeResult::Ready,
         "valid double FSceneViewInitOptions must decode");
  expect(camera.camera_id == 0x000001BC12345000ULL && camera.sample_index == 7 &&
             camera.sample_time_ns == 99 && camera.active,
         "decoded camera identity and timing mismatch");
  expect(std::abs(camera.location_uu.x - 100.0) < 1e-9 &&
             std::abs(camera.location_uu.y + 25.0) < 1e-9 &&
             std::abs(camera.location_uu.z - 175.0) < 1e-9,
         "decoded camera origin mismatch");
  expect(std::abs(camera.rotation_degrees.x) < 1e-6 &&
             std::abs(camera.rotation_degrees.y) < 1e-6 &&
             std::abs(camera.rotation_degrees.z) < 1e-6,
         "axis-permuted identity view must decode to zero UE rotation");
  expect(std::abs(camera.vertical_fov_degrees - 90.0F) < 0.001F &&
             std::abs(camera.aspect_ratio - (16.0F / 9.0F)) < 0.001F &&
             std::abs(camera.near_clip_uu - 10.0F) < 0.01F &&
             std::abs(camera.far_clip_uu - 1000.0F) < 0.1F,
         "decoded projection mismatch");
  expect(identity.view_family == 0x0000007E12345000ULL &&
             identity.scene_view_state == 0x000001BC12345000ULL &&
             identity.player_index == -1 && identity.stereo_pass == -1,
         "decoded view identity metadata mismatch");

  putU64(0x160, 0);
  putU64(0x168, 0x0000043800000780ULL);
  expect(decodeMeccha24508135SceneViewInitOptions(
             raw.data(), raw.size(), 8, 100, &camera) ==
             UnrealSceneViewDecodeResult::MissingIdentity,
         "a 1920x1080 rectangle must never masquerade as camera identity");
  putU64(0x160, 0x000001BC12345000ULL);
  putU64(0x168, 0);

  const double yaw_90_view[16] = {
      -1, 0, 0, 0,
       0, 0, 1, 0,
       0, 1, 0, 0,
       0, 0, 0, 1};
  std::memcpy(raw.data() + 0x20, yaw_90_view, sizeof(yaw_90_view));
  expect(decodeMeccha24508135SceneViewInitOptions(
             raw.data(), raw.size(), 8, 100, &camera) ==
             UnrealSceneViewDecodeResult::Ready &&
             std::abs(camera.rotation_degrees.x) < 1e-6 &&
             std::abs(camera.rotation_degrees.y - 90.0) < 1e-6 &&
             std::abs(camera.rotation_degrees.z) < 1e-6,
         "UE axis-permuted view matrix must recover a +90 degree yaw");

  putDouble(0x08, std::numeric_limits<double>::quiet_NaN());
  expect(decodeMeccha24508135SceneViewInitOptions(
             raw.data(), raw.size(), 8, 100, &camera) ==
             UnrealSceneViewDecodeResult::NonFinite,
         "non-finite scene view data must fail closed");
}

void testFSceneViewPeReader() {
  using namespace vrclient::adapters::unreal;
  wchar_t executable[MAX_PATH]{};
  expect(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0,
         "test executable path unavailable");
  UnrealViewSeamImage image{};
  std::string error;
  expect(
      loadUnrealViewSeamImageFromFile(executable, &image, &error) ==
              UnrealViewSeamPeResult::Ready &&
          !image.text.bytes.empty() && !image.rdata.bytes.empty() &&
          !image.runtime_functions.empty(),
      "real x64 PE sections and runtime functions must load: " + error);
}

void testOpenXrBridgeContract() {
  using namespace vrclient::adapters::unreal;
  auto graphics = makeGraphics();

  D3D12Observer observer;
  expect(
      observer.observeDeviceQueue(
          graphics.device.Get(),
          graphics.direct_queue.Get()) == D3D12ObservationResult::Pending,
      "bridge fixture device/queue observation failed");
  expect(
      observer.observeSwapchain(graphics.swapchain.Get()) ==
          D3D12ObservationResult::Ready,
      "bridge fixture swapchain observation failed");

  VrRuntimeD3D12Binding runtime_binding{};
  expect(
      makeOpenXrD3D12Binding(observer.snapshot(), &runtime_binding) ==
          UnrealOpenXrBridgeResult::Ready,
      "ready observation should become an OpenXR borrowed binding");
  expect(
      runtime_binding.size == sizeof(runtime_binding) &&
          runtime_binding.device == graphics.device.Get() &&
          runtime_binding.queue == graphics.direct_queue.Get(),
      "bridge must preserve the exact observed device and queue");

  VrRuntimeFrameData frame{};
  frame.timing.frame_index = 42;
  frame.timing.predicted_display_time_ns = 1234567;
  frame.eye_count = 2;
  frame.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
  frame.eyes[1].eye = VR_RUNTIME_EYE_RIGHT;
  for (auto& eye : frame.eyes) {
    eye.view.m[0] = 1.0f;
    eye.projection.m[0] = 1.0f;
  }
  VrRuntimeRenderTarget targets[2]{};
  targets[0] = {
      VR_RUNTIME_GRAPHICS_BACKEND_D3D12,
      VR_RUNTIME_EYE_LEFT,
      reinterpret_cast<void*>(1),
      0,
      16,
      8,
      {0, 0, 16, 8},
      DXGI_FORMAT_R8G8B8A8_UNORM,
  };
  targets[1] = targets[0];
  targets[1].eye = VR_RUNTIME_EYE_RIGHT;
  targets[1].color_texture = reinterpret_cast<void*>(2);

  UnrealFrameSubmission submission{};
  expect(
      makeUnrealFrameSubmission(frame, targets, 2, &submission) ==
          UnrealOpenXrBridgeResult::Ready,
      "two OpenXR eye targets should become an Unreal frame submission");
  expect(
      submission.frame_index == 42 &&
          submission.eyes[0].color_target == targets[0].color_texture &&
          submission.eyes[1].color_target == targets[1].color_texture,
      "bridge must preserve frame identity and distinct eye resources");

  auto first_frame = frame;
  first_frame.timing.frame_index = 0;
  expect(
      makeUnrealFrameSubmission(first_frame, targets, 2, &submission) ==
              UnrealOpenXrBridgeResult::Ready &&
          submission.frame_index == 0,
      "the OpenXR runtime's zero-based first frame must be accepted");

  targets[1].eye = VR_RUNTIME_EYE_LEFT;
  expect(
      makeUnrealFrameSubmission(frame, targets, 2, &submission) ==
          UnrealOpenXrBridgeResult::InvalidStereoTargets,
      "duplicate eye labels must fail closed");
}

void testSideBySideRelayUsesDistinctEyeRegions() {
  using namespace vrclient::adapters::unreal;

  const auto left = sceneRelayUvTransform(
      D3D12SceneRelaySourceLayout::SideBySideStereo,
      VR_RUNTIME_EYE_LEFT);
  const auto right = sceneRelayUvTransform(
      D3D12SceneRelaySourceLayout::SideBySideStereo,
      VR_RUNTIME_EYE_RIGHT);
  expect(
      std::abs(left.scale_u - 0.5F) < 1.0e-6F &&
          std::abs(left.bias_u) < 1.0e-6F &&
          std::abs(right.scale_u - 0.5F) < 1.0e-6F &&
          std::abs(right.bias_u - 0.5F) < 1.0e-6F,
      "side-by-side relay must sample a different half for each eye");

  const auto mono = sceneRelayUvTransform(
      D3D12SceneRelaySourceLayout::Monoscopic,
      VR_RUNTIME_EYE_RIGHT);
  expect(
      std::abs(mono.scale_u - 1.0F) < 1.0e-6F &&
          std::abs(mono.bias_u) < 1.0e-6F,
      "monoscopic relay must preserve the complete source image");
}

void testSideBySideRelayPreservesConfiguredProjectionAspect() {
  using namespace vrclient::adapters::unreal;
  D3D12SceneRelayProjection resolved{};
  const D3D12SceneRelayProjection configured{110.0F, 1.0F};
  expect(
      sideBySideProjectionForPackedTexture(
          configured, 1920, 1080, &resolved) &&
          std::abs(resolved.vertical_fov_degrees - 110.0F) < 0.00001F &&
          std::abs(resolved.eye_aspect_ratio - 1.0F) < 0.00001F,
      "packed texture shape must preserve the engine projection aspect");
  expect(
      !sideBySideProjectionForPackedTexture(
          configured, 1919, 1080, &resolved) &&
          !sideBySideProjectionForPackedTexture(
              configured, 1920, 0, &resolved) &&
          !sideBySideProjectionForPackedTexture(
              configured, 1920, 1080, nullptr),
      "invalid or uneven stereo packing must fail closed");
}

void testStereoRelayProjectionMapsOpenXrFovIntoWideSource() {
  using namespace vrclient::adapters::unreal;

  D3D12SceneRelayProjection source{};
  source.vertical_fov_degrees = 110.0F;
  source.eye_aspect_ratio = 16.0F / 9.0F;
  VrRuntimeEyeView eye{};
  eye.fov_angle_left = -0.82F;
  eye.fov_angle_right = 0.70F;
  eye.fov_angle_up = 0.76F;
  eye.fov_angle_down = -0.84F;

  D3D12SceneRelayProjectionUv top_left{};
  D3D12SceneRelayProjectionUv bottom_right{};
  D3D12SceneRelayProjectionUv center{};
  expect(
      mapOpenXrFovToSceneRelayProjection(
          source, eye, 0.0F, 0.0F, &top_left) &&
          mapOpenXrFovToSceneRelayProjection(
              source, eye, 1.0F, 1.0F, &bottom_right) &&
          mapOpenXrFovToSceneRelayProjection(
              source, eye, 0.5F, 0.5F, &center),
      "valid OpenXR FOV must map into the configured wide source projection");
  expect(
      top_left.u > 0.0F && top_left.v > 0.0F &&
          bottom_right.u < 1.0F && bottom_right.v < 1.0F &&
          top_left.u < center.u && center.u < bottom_right.u &&
          top_left.v < center.v && center.v < bottom_right.v,
      "Quest-class target rays must remain ordered inside a 110-degree source");
  expect(
      std::abs(center.u - 0.5F) > 1.0e-3F &&
          std::abs(center.v - 0.5F) > 1.0e-3F,
      "asymmetric OpenXR FOV must shift the source projection center");

  eye.fov_angle_right = 1.55F;
  expect(
      !mapOpenXrFovToSceneRelayProjection(
          source, eye, 1.0F, 0.5F, &center),
      "a target FOV wider than the source must fail closed instead of stretching edges");
}

}  // namespace

int main() {
  try {
    testReadinessContract();
    testDiscoveryAndSubmissionContracts();
    testLiveCameraSampleContract();
    testNeutralOrientationOnlyPosePreservesView();
    testOrientationOnlySubGimbalAxesMatchStereoDirections();
    testOrientationOnlyPostComposesHeadsetYawAfterBasePitch();
    testOrientationOnlyPostComposesHeadsetPitchAfterBaseYaw();
    testOrientationOnlyAcceptsRuntimeFrameZero();
    testOrientationOnlyPoseFailsClosed();
    testOrientationOnlySceneViewWriteTouchesRotationOnly();
    testStableCameraObservation();
    testOrientationIdentityGateRejectsRecentCompetitor();
    testStereoIdentityGateRequiresExactlyOneStablePair();
    testStereoPosePairUsesOnePoseGenerationForBothEyes();
    testStereoPosePairKeepsDuplicatesCoherentAndCountsIncompleteFrames();
    testStereoProjectionGateRequiresConfiguredEngineFov();
    testMecchaStereoProjectionNormalizationTouchesOnlyAngularScales();
    testMecchaCameraPinTargetsBuild24517175();
    testOrientationTrackingGateRewarmsAfterTransientFreshnessLoss();
    testOrientationTrackingStateRewarmsAfterPublisherGap();
    testOrientationTrackingStateKeepsConcurrentFaultLatched();
    testOrientationTelemetrySnapshotNeverMixesGenerations();
    testOrientationOperationalSnapshotRejectsMixedCounterGenerations();
    testFSceneViewLocator();
    testPinnedFSceneViewLocator();
    testDoubleFSceneViewDecoder();
    testFSceneViewPeReader();
    testObserverAndBorrowedBinding();
    testOpenXrBridgeContract();
    testSideBySideRelayUsesDistinctEyeRegions();
    testSideBySideRelayPreservesConfiguredProjectionAspect();
    testStereoRelayProjectionMapsOpenXrFovIntoWideSource();
    std::cout << "native Unreal contract and D3D12 observation tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
