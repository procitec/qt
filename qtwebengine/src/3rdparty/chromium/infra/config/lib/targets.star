# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for defining targets that the chromium family of recipes can build/test."""

load("@stdlib//internal/graph.star", "graph")
load("@stdlib//internal/luci/common.star", "keys")
load("./args.star", "args")
load("./nodes.star", "nodes")

# The binary for a target. gtests and isolated scripts must reference a binary,
# or it defaults to one with the same name  as the test.
_TARGET_BINARY = nodes.create_unscoped_node_type("target-binary")
_TARGET_MIXIN = nodes.create_unscoped_node_type("target-mixin")
_TARGET_VARIANT = nodes.create_unscoped_node_type("target-variant")

# A test that can be included in a basic suite
_LEGACY_TEST = nodes.create_unscoped_node_type("legacy-test")
_LEGACY_BASIC_SUITE = nodes.create_unscoped_node_type("legacy-basic-suite")
_LEGACY_COMPOUND_SUITE = nodes.create_unscoped_node_type("legacy-compound-suite")
_LEGACY_MATRIX_COMPOUND_SUITE = nodes.create_unscoped_node_type("legacy-matrix-compound-suite")
_LEGACY_MATRIX_CONFIG = nodes.create_scoped_node_type("legacy-matrix-config", _LEGACY_MATRIX_COMPOUND_SUITE.kind)

_COMPILE_TARGET = nodes.create_unscoped_node_type("compile-target")
TARGET_BUNDLE = nodes.create_unscoped_node_type("bundle", allow_unnamed = True)

def _binary_test_config(*, results_handler = None, merge = None, resultdb = None):
    """The details for a test provided by the test's binary.

    When test_suites.pyl is generated, tests that are using the binary
    will have these values written into the test's entry in the basic suite.

    Args:
        results_handler: The name of the results handler to use for the
            test.
        merge: A targets.merge describing the invocation to merge the
            results from the test's tasks.
        resultdb: A targets.resultdb describing the ResultDB integration
            for the test.
    """
    return struct(
        results_handler = results_handler,
        merge = merge,
        resultdb = resultdb,
    )

def _create_compile_target(*, name):
    _COMPILE_TARGET.add(name)

def _create_binary(
        *,
        name,
        type,
        label,
        label_type = None,
        executable = None,
        executable_suffix = None,
        script = None,
        skip_usage_check = False,
        args = None,
        test_config = None):
    binary_key = _TARGET_BINARY.add(name, props = dict(
        type = type,
        label = label,
        label_type = label_type,
        executable = executable,
        executable_suffix = executable_suffix,
        script = script,
        skip_usage_check = skip_usage_check,
        args = args,
        test_config = test_config,
    ))
    graph.add_edge(keys.project(), binary_key)

    _create_compile_target(
        name = name,
    )

def _basic_suite_test_config(
        *,
        script = None,
        binary = None,
        telemetry_test_name = None,
        args = None,
        mixins = None):
    """The details for the test included when included in a basic suite.

    When generating test_suites.pyl, these values will be written out
    for a test in any basic suite that includes it.

    Args:
        script: The name of the file within the //testing/scripts
            directory to run as the test. Only applicable to script tests.
        binary: The name of the binary to run as the test. Only
            applicable to gtests, isolated script tests and junit tests.
        telemetry_test_name: The telemetry test to run. Only applicable
            to telemetry test types.
        args: Arguments to be passed to the test binary.
        mixins: Mixins to apply when expanding the test.
    """
    return struct(
        script = script,
        binary = binary,
        telemetry_test_name = telemetry_test_name,
        mixins = mixins,
        args = args,
    )

def _create_legacy_test(*, name, basic_suite_test_config):
    return _LEGACY_TEST.add(name, props = dict(
        basic_suite_test_config = basic_suite_test_config,
    ))

def _create_bundle(*, name, additional_compile_targets = [], targets = [], builder_group = None, test_spec_by_name = {}, modifications_by_name = {}):
    key = TARGET_BUNDLE.add(name, props = dict(
        builder_group = builder_group,
        additional_compile_targets = set(additional_compile_targets),
        test_spec_by_name = test_spec_by_name,
        modifications_by_name = modifications_by_name,
    ))

    # We won't actually traverse the edge for compile targets, but adding the
    # edge ensures that the compile target has been declared
    for t in additional_compile_targets:
        graph.add_edge(key, _COMPILE_TARGET.key(t))
    for t in targets:
        graph.add_edge(key, TARGET_BUNDLE.key(t))
    return key

def _create_test_target(*, name, spec_type, spec_value):
    return _create_bundle(
        name = name,
        test_spec_by_name = {
            name: struct(
                spec_type = spec_type,
                spec_value = spec_value,
            ),
        },
    )

def _spec_type(*, finalize):
    return struct(
        finalize = finalize,
    )

# TODO: crbug.com/1420012 - Update the handling of unimplemented test types so
# that more context is provided about where the error is resulting from
def _unimplemented_target_type(type_name):
    def unimplemented():
        fail("support for {} targets is not yet implemented".format(type_name))

    return _spec_type(
        finalize = (lambda name, spec: unimplemented()),
    )

def _compile_target(*, name, label = None, skip_usage_check = False):
    """Define a compile target to use in targets specs.

    A compile target provides a mapping to any ninja target that will
    only be built, not executed.

    Args:
        name: The name that can be used to refer to the target.
        label: The GN label for the ninja target.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
    """

    # The all target is a special ninja target that doesn't map to a GN label
    # and so we don't create an entry in gn_isolate_map.pyl
    if name == "all":
        if label != None:
            fail("label should not be set for compile target all")
        _create_compile_target(
            name = name,
        )
        return

    if label == None:
        fail("label must be set in compile_target {}".format(name))
    _create_binary(
        name = name,
        type = "additional_compile_target",
        label = label,
        skip_usage_check = skip_usage_check,
    )

def _console_test_launcher(
        *,
        name,
        label,
        label_type = None,
        skip_usage_check = False,
        args = None):
    """Define a console test launcher target to use in targets specs.

    A console test launcher is a gtest-based test that uses the
    parallelizing TestLauncher from //base/test:test_support but does
    not need Xvfb.

    Args:
        name: The name that can be used to refer to the target.
        label: The GN label for the ninja target.
        label_type: The type of the label. This is used by MB to find
            the generated runtime files in the correct place if the
            target uses the test_launcher command-line conventions but
            the label refers to a different type of target.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
        args: The arguments to the test. These arguments will be
            included when the test is run using "mb try"
    """
    _create_binary(
        name = name,
        type = "console_test_launcher",
        label = label,
        label_type = label_type,
        skip_usage_check = skip_usage_check,
        args = args,
    )

def _generated_script(
        *,
        name,
        label,
        skip_usage_check = False,
        args = None,
        results_handler = None,
        merge = None,
        resultdb = None):
    """Define a generated script target to use in targets specs.

    A generated script target is a test that is executed via a script
    generated at build time. The script must be in
    output_dir/bin/run_$target (or output_dir\bin\run_$target.bat on
    Windows).

    Args:
        name: The name that can be used to refer to the target.
        label: The GN label for the ninja target.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
        args: The arguments to the test. These arguments will be
            included when the test is run using "mb try"
        results_handler: The name of the results handler to use for the
            test.
        merge: A targets.merge describing the invocation to merge the
            results from the test's tasks.
        resultdb: A targets.resultdb describing the ResultDB integration
            for the test.
    """
    _create_binary(
        name = name,
        type = "generated_script",
        label = label,
        skip_usage_check = skip_usage_check,
        args = args,
        test_config = _binary_test_config(
            results_handler = results_handler,
            merge = merge,
            resultdb = resultdb,
        ),
    )

def _script(
        *,
        name,
        label,
        script,
        skip_usage_check = False,
        args = None,
        merge = None,
        resultdb = None):
    """Define a script target to use in targets specs.

    A script target is a test that is executed via a python script.

    Args:
        name: The name that can be used to refer to the target.
        label: The GN label for the ninja target.
        script: The GN path (e.g. //testing/scripts/foo.py" to the python
            script to run.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
        args: The arguments to the test. These arguments will be
            included when the test is run using "mb try"
        merge: A targets.merge describing the invocation to merge the
            results from the test's tasks.
        resultdb: A targets.resultdb describing the ResultDB integration
            for the test.
    """
    _create_binary(
        name = name,
        type = "script",
        label = label,
        script = script,
        skip_usage_check = skip_usage_check,
        args = args,
        test_config = _binary_test_config(
            merge = merge,
            resultdb = resultdb,
        ),
    )

def _windowed_test_launcher(
        *,
        name,
        label,
        label_type = None,
        executable = None,
        executable_suffix = None,
        skip_usage_check = False,
        args = None):
    """Define a windowed test launcher target to use in targets specs.

    A windowed test launcher is a gtest-based test that uses the
    parallelizing TestLauncher from //base/test:test_support and needs
    to run under Xvfb if run on some platforms (eg. Linux Desktop and
    Ozone CrOS).

    Args:
        name: The name that can be used to refer to the target.
        label: The GN label for the ninja target.
        label_type: The type of the label. This is used by MB to find
            the generated runtime files in the correct place if the
            target uses the test_launcher command-line conventions but
            the label refers to a different type of target.
        executable: The binary to run. By default, the ninja target name
            will be used. On Windows, .exe will be appended, so it
            should not appear in the executable name.
        executable_suffix: The suffix to append to the executable name.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
        args: The arguments to the test. These arguments will be
            included when the test is run using "mb try"
    """
    _create_binary(
        name = name,
        type = "windowed_test_launcher",
        label = label,
        label_type = label_type,
        executable = executable,
        executable_suffix = executable_suffix,
        skip_usage_check = skip_usage_check,
        args = args,
    )

# TODO(gbeaty) The args that are specified for webgl2?_conformance(.*)_tests
# in the basic suites are pretty formulaic, it would probably make sense to lift
# many of those values into this function
def _gpu_telemetry_test(
        *,
        name,
        telemetry_test_name = None,
        args = None,
        mixins = None):
    """Define a GPU telemetry test.

    A GPU telemetry test can be included in a basic suite to run the
    test for any builder that includes that basic suite.

    Args:
        name: The name that can be used to refer to the test in other
            starlark declarations. The step name of the test will be
            based on this name (additional components may be added by
            the recipe or when generating a test with a variant).
        telemetry_test_name: The name of the telemetry benchmark to run.
        mixins: Mixins to apply when expanding the test.
    """
    _create_legacy_test(
        name = name,
        basic_suite_test_config = _basic_suite_test_config(
            telemetry_test_name = telemetry_test_name,
            args = args,
            mixins = mixins,
        ),
    )
    _create_test_target(
        name = name,
        spec_type = _unimplemented_target_type("gpu_telemetry_test"),
        spec_value = None,
    )

def _gtest_test(*, name, binary = None, mixins = None, args = None):
    """Define a gtest-based test.

    A gtest test can be included in a basic suite to run the test for
    any builder that includes that basic suite.

    Args:
        name: The name that can be used to refer to the test in other
            starlark declarations. The step name of the test will be
            based on this name (additional components may be added by
            the recipe or when generating a test with a variant).
        binary: The test binary to use. There must be a defined binary
            with the given name. If none is provided, then an binary
            with the same name as the test must be defined.
        mixins: Mixins to apply when expanding the test.
        args: Arguments to be passed to the test binary.
    """
    key = _create_legacy_test(
        name = name,
        basic_suite_test_config = _basic_suite_test_config(
            binary = binary,
            mixins = mixins,
            args = args,
        ),
    )

    # Make sure that the binary actually exists
    graph.add_edge(key, _TARGET_BINARY.key(binary or name))

    _create_test_target(
        name = name,
        spec_type = _unimplemented_target_type("gtest_test"),
        spec_value = None,
    )

def _isolated_script_test(*, name, binary = None, mixins = None, args = None):
    """Define an isolated script test.

    An isolated script test can be included in a basic suite to run the
    test for any builder that includes that basic suite.

    Args:
        name: The name that can be used to refer to the test in other
            starlark declarations. The step name of the test will be
            based on this name (additional components may be added by
            the recipe or when generating a test with a variant).
        binary: The test binary to use. There must be a defined binary
            with the given name. If none is provided, then an binary
            with the same name as the test must be defined.
        mixins: Mixins to apply when expanding the test.
        args: Arguments to be passed to the test binary.
    """
    key = _create_legacy_test(
        name = name,
        basic_suite_test_config = _basic_suite_test_config(
            binary = binary,
            mixins = mixins,
            args = args,
        ),
    )

    # Make sure that the binary actually exists
    graph.add_edge(key, _TARGET_BINARY.key(binary or name))

    _create_test_target(
        name = name,
        spec_type = _unimplemented_target_type("isolated_script_test"),
        spec_value = None,
    )

def _junit_test(*, name, label, skip_usage_check = False):
    """Define a junit test.

    A junit test is a test using the JUnit test framework. A junit test
    can be included in a basic suite to run the test for any builder
    that includes that basic suite.

    Args:
        name: The name that can be used to refer to the test in other
            starlark declarations. The step name of the test will be
            based on this name (additional components may be added by
            the recipe or when generating a test with a variant).
        label: The GN label for the ninja target.
        skip_usage_check: Disables checking that the target is actually
            referenced in a targets spec for some builder.
    """

    # We don't need to reuse the test binary for multiple junit tests, so just
    # define the isolate entry as part of the test declaration
    _create_binary(
        name = name,
        type = "generated_script",
        label = label,
        skip_usage_check = skip_usage_check,
    )
    _create_legacy_test(
        name = name,
        basic_suite_test_config = _basic_suite_test_config(),
    )

    _create_test_target(
        name = name,
        spec_type = _unimplemented_target_type("junit_test"),
        spec_value = None,
    )

_script_test_target_type = _spec_type(
    finalize = (lambda name, spec: ("scripts", name, spec)),
)

def _script_test(*, name, script):
    """Define a script test.

    A script test is a test that runs a python script wihin the
    //testing/scripts directory.

    Args:
        name: The name that can be used to refer to the test in other
            starlark declarations. The step name of the test will be
            based on this name (additional components may be added by
            the recipe or when generating a test with a variant).
        script: The name of the file within the //testing/scripts
            directory to run as the test.
    """
    _create_legacy_test(
        name = name,
        basic_suite_test_config = _basic_suite_test_config(
            script = script,
        ),
    )

    _create_test_target(
        name = name,
        spec_type = _script_test_target_type,
        spec_value = dict(
            name = name,
            script = script,
        ),
    )

def _cipd_package(
        *,
        package,
        location,
        revision):
    """Define a CIPD package to be downloaded by a swarmed test.

    Args:
        package: The package to download.
        location: The path relative to the task's execution directory to
            download the package to.
        revision: The revision of the package to download.

    Returns:
        A struct that can be passed as an element of the cipd_packages
        argument of targets.swarming.
    """
    return struct(
        package = package,
        location = location,
        revision = revision,
    )

def _merge(
        *,
        script,
        args = None):
    """Define a merge script to be used for a swarmed test.

    Args:
        script: GN-format path (e.g. //foo/bar/script.py) to the script
            to use to merge results from the shard tasks.
        args: Any args to pass to the merge script, in addition to any
            arguments supplied by the recipe.

    Returns:
        A struct that can be passed to the merge argument of
        `targets.mixin`.
    """
    return struct(
        script = script,
        args = args,
    )

def _resultdb(
        *,
        enable = False,
        has_native_resultdb_integration = False,
        result_format = None,
        result_file = None):
    """Define the ResultDB integration to be used for a test.

    Args:
        enable: Whether or not ResultDB is enabled for the test.
        has_native_resultdb_integration: Whether or not the test has
            native integration with resultdb. If not, result_adapter
            will be used, which parses output to determine results to
            upload results to ResultDB.
        result_format: The format of the test results.
        result_file: The file to write out test results to.

    Return:
        A struct that can be passed to the resultdb argument of
        `target.mixin`
    """
    return struct(
        enable = enable,
        has_native_resultdb_integration = has_native_resultdb_integration,
        result_format = result_format,
        result_file = result_file,
    )

def _swarming(
        *,
        enable = None,
        dimensions = None,
        optional_dimensions = None,
        containment_type = None,
        cipd_packages = None,
        expiration_sec = None,
        hard_timeout_sec = None,
        io_timeout_sec = None,
        shards = None,
        idempotent = None,
        service_account = None,
        named_caches = None):
    """Define the swarming details for a test.

    When specified as a mixin, fields will overwrites the test's values
    unless otherwise indicated.

    Args:
        enable: Whether swarming should be enabled for the test.
        dimensions: A dict of dimensions to apply to all dimension sets
            for the test. This can only be specified in a mixin. After
            any dimension sets from the mixin are added to the test, the
            dimensions will be applied to all of the dimension sets on
            the test. If there are no dimension sets on the test, a
            single dimension set with these dimensions will be added.
        optional_dimensions: Optional dimensions to add to each
            dimension set.
        containment_type: The containment type to use for the swarming
            task(s). See ContainmentType enum in
            https://source.chromium.org/chromium/infra/infra/+/main:go/src/go.chromium.org/luci/swarming/proto/api/swarming.proto
        cipd_packages: A list of targets.cipd_package that detail CIPD
            packages to be downloaded for the test.
        expiration_sec: The time that each task for the test should wait
            to be scheduled.
        hard_timeout_sec: The maximum time each task for the test can
            take after starting.
        io_timeout_sec: The maximum time that can elapse between output
            from tasks for the test.
        shards: The number of tasks to split the test into.
        idempotent: Whether the test task should be considered
            idempotent.
        service_account: The service account used to run the test's
            tasks.
        named_caches: A list of swarming.cache that detail the named
            caches that should be mounted for the test's tasks.
    """
    return struct(
        enable = enable,
        dimensions = dimensions,
        optional_dimensions = optional_dimensions,
        containment_type = containment_type,
        cipd_packages = cipd_packages,
        expiration_sec = expiration_sec,
        hard_timeout_sec = hard_timeout_sec,
        io_timeout_sec = io_timeout_sec,
        shards = shards,
        idempotent = idempotent,
        service_account = service_account,
        named_caches = named_caches,
    )

def _skylab(
        *,
        cros_board,
        cros_img = "",
        use_lkgm = False,
        cros_model = None,
        autotest_name = None,
        bucket = None,
        dut_pool = None,
        public_builder = None,
        public_builder_bucket = None,
        shards = None):
    """Define a Skylab test target.

    Args:
        cros_board: The CrOS build target name, e.g. "eve", "kevin".
        cros_img: ChromeOS image version to be deployed to DUT.
            Must be empty when use_lkgm is true.
            For example, "brya-release/R118-15604.42.0"
        use_lkgm: If True, use a ChromeOS image version derived from
            chromeos/CHROMEOS_LKGM file.
        cros_model: Optional ChromeOS DUT model.
        autotest_name: The name of the autotest to be executed in
            Skylab.
        bucket: Optional Google Storage bucket where the specified
            image(s) are stored.
        dut_pool: The skylab device pool to run the test. By default the
            quota pool, shared by all CrOS tests.
        public_builder: Optional Public CTP Builder.
            The public_builder and public_builder_bucket fields can be
            used when default CTP builder is not sufficient/advised
            (ex: chromium cq, satlab for partners).
        public_builder_bucket: Optional luci bucket. See public_builder
            above.
        shards: The number of shards used to run the test.
    """
    return struct(
        cros_board = cros_board,
        cros_img = cros_img,
        use_lkgm = use_lkgm,
        cros_model = cros_model,
        autotest_name = autotest_name,
        bucket = bucket,
        dut_pool = dut_pool,
        public_builder = public_builder,
        public_builder_bucket = public_builder_bucket,
        shards = shards,
    )

def _mixin_values(
        description = None,
        args = None,
        precommit_args = None,
        android_args = None,
        chromeos_args = None,
        desktop_args = None,
        lacros_args = None,
        linux_args = None,
        mac_args = None,
        win_args = None,
        win64_args = None,
        swarming = None,
        android_swarming = None,
        chromeos_swarming = None,
        skylab = None,
        use_isolated_scripts_api = None,
        ci_only = None,
        check_flakiness_for_new_tests = None,
        resultdb = None,
        isolate_profile_data = None,
        merge = None,
        timeout_sec = None,
        shards = None,
        experiment_percentage = None):
    """Define values to be mixed into a target.

    Unless otherwise specified, each field will overwrite an existing
    value on a test.

    Args:
        description: A description to attach to the test. If specified,
            it will be appended to any description already present on
            the test.
        args: Arguments to be passed to the test binary. Will be
            appended to any existing args for the test.
        precommit_args: Arguments to be passed to the test binary when
            running on a try builder. Will be appended to any existing
            precommit_args for the test. These are applied at
            build-time, so they will always be emitted into the test
            spec if present.
        android_args: Arguments to be passed to the test when the
            builder is targeting android. Will be appended to any
            existing android_args for the test.
        chromeos_args: Arguments to be passed to the test when the
            builder is targeting chromeos. Will be appended to any
            existing chromeos_args for the test.
        desktop_args: Arguments to be passed to the test when the
            builder is targeting linux, mac or windows. Will be appended
            to any existing desktop_args for the test.
        linux_args: Arguments to be passed to the test when the builder
            is targeting linux. Will be appended to any existing
            linux_args for the test.
        lacros_args: Arguments to be passed to the test when the
            builder is targeting lacros. Will be appended to any
            existing lacros_args for the test.
        mac_args: Arguments to be passed to the test when the builder is
            targeting mac. Will be appended to any existing mac_args for
            the test.
        win_args: Arguments to be passed to the test when the builder
            is targeting win. Will be appended to any existing
            win_args for the test.
        win64_args: Arguments to be passed to the test when the builder
            is targeting win64. Will be appended to any existing
            win64_args for the test.
        swarming: A targets.swarming to be applied to the test. See
            targets.swarming for details about how each field is applied
            to the test's swarming details.
        android_swarming: A targets.swarming to be applied to the test
            when the builder is targeting android.
        chromeos_swarming: A targets.swarming to be applied to the test
            when the builder is targeting chromeos.
        skylab: A targets.skylab to be applied to the test. See
            targets.skylab for details about how each field is applied
            to the test.
        use_isolated_scripts_api: A bool indicating whether to use the
            isolated scripts interface to run the test. Only
            applicable to gtests.
        ci_only: A bool indicating whether the test should only be run
            in CI by default.
        check_flakiness_for_new_tests: A bool indicating whether try
            builders running the test should rerun new tests additional
            times to check for flakiness.
        resultdb: A targets.resultdb describing the ResultDB integration
            for the test.
        isolate_profile_data: A bool indicating whether profile data for
            the test should be included in the test tasks' output
            isolates.
        merge: A targets.merge describing the invocation to merge the
            results from the test's tasks.
        timeout_sec: The maximum time the test can take to run.
        shards: The number of shards to use for running the test on
            skylab.
        experiment_percentage: An integer in the range [0, 100]
            indicating the percentage chance that the test will be run,
            with failures in the test not resulting in failures in the
            build.

    Returns:
        A dict containing the values to be mixed in.
    """
    mixin_values = dict(
        description = description,
        args = args,
        precommit_args = precommit_args,
        android_args = android_args,
        chromeos_args = chromeos_args,
        desktop_args = desktop_args,
        lacros_args = lacros_args,
        linux_args = linux_args,
        mac_args = mac_args,
        win_args = win_args,
        win64_args = win64_args,
        swarming = swarming,
        android_swarming = android_swarming,
        chromeos_swarming = chromeos_swarming,
        skylab = skylab,
        use_isolated_scripts_api = use_isolated_scripts_api,
        ci_only = ci_only,
        check_flakiness_for_new_tests = check_flakiness_for_new_tests,
        resultdb = resultdb,
        isolate_profile_data = isolate_profile_data,
        merge = merge,
        timeout_sec = timeout_sec,
        shards = shards,
        experiment_percentage = experiment_percentage,
    )
    return {k: v for k, v in mixin_values.items() if v != None}

def _mixin(*, name = None, **kwargs):
    """Define a mixin used for defining tests.

    //infra/config/generated/testing/mixins.pyl will be generated from
    the declared mixins to be copied to //testing/buildbot and consumed
    by //testing/buildbot/generate_buildbot_json.py.

    Args:
        name: The name of the mixin.
        **kwargs: The mixin values, see _mixin_values for allowed
            keywords and their meanings.
    """
    key = _TARGET_MIXIN.add(name, props = dict(
        mixin_values = _mixin_values(**kwargs),
    ))

    graph.add_edge(keys.project(), key)

def _variant(
        *,
        name,
        identifier,
        enabled = None,
        mixins = None,
        **kwargs):
    """Define a variant used for defining tests.

    //infra/config/generated/testing/variants.pyl will be generated from
    the declared variants to be copied to //testing/buildbot and consumed
    by //testing/buildbot/generate_buildbot_json.py.

    Args:
        name: The name of the variant.
        identifier: A string suitable for display to users that
            identifies the variant of the test being run. When tests are
            expanded with the variant, this will be appended to the test
            name.
        enabled: Whether or not the variant is enabled. By default, a
            variant is enabled. If a variant is not enabled, then it
            will be ignored when expanding a test suite with variants.
        mixins: Names of mixins to apply when expanding a test with the
            variant.
        **kwargs: The mixin values, see _mixin_values for allowed
            keywords and their meanings.
    """
    if enabled == None:
        enabled = True
    key = _TARGET_VARIANT.add(name, props = dict(
        identifier = identifier,
        enabled = enabled,
        mixins = mixins,
        mixin_values = _mixin_values(**kwargs),
    ))

    graph.add_edge(keys.project(), key)

def _bundle(*, name = None, additional_compile_targets = None, targets = None):
    """Define a targets bundle.

    A bundle is a grouping of targets to build and test.

    Args:
        name: The name of the bundle.
        targets: A list of targets, bundles or legacy basic suites to
            include in the bundle.
    """
    return graph.keyset(_create_bundle(
        name = name,
        additional_compile_targets = args.listify(additional_compile_targets),
        targets = args.listify(targets),
    ))

def _legacy_basic_suite(*, name, tests):
    """Define a basic suite.

    //infra/config/generated/testing/test_suites.pyl will be generated from the
    declared basic suites, as well as declared compound suites (see
    _legacy_compound_suite) and declared matrix compound suites (see
    _legacy_matrix_compound_suite) to be copied to //testing/buildbot and
    consumed by //testing/buildbot/generate_buildbot_json.py.

    A basic suite is a collection of tests that can be specified in
    waterfalls.pyl as the test suite for one of the test types or collected into
    a compound suite or matrix compound suite.

    Args:
        name: The name of the suite.
        tests: A dict mapping the name of the test to the base definition for
            the test, which must be an instance returned from
            targets.legacy_test_config.
    """
    key = _LEGACY_BASIC_SUITE.add(name, props = dict(
        tests = tests,
    ))
    graph.add_edge(keys.project(), key)
    for t, config in tests.items():
        if not config:
            fail("The value for test {} in basic suite {} must be an object returned from targets.legacy_test_config"
                .format(t, name))
        graph.add_edge(key, _LEGACY_TEST.key(t))

def _legacy_test_config(
        *,
        # TODO(gbeaty) Tast tests should have their own test function defined
        # and this should be removed from this function
        tast_expr = None,
        # TODO(gbeaty) Skylab details should be modified to be under a separate
        # structure like swarming details are and this should be made a part of
        # mixins and removed from this function
        test_level_retries = None,
        mixins = None,
        remove_mixins = None,
        **kwargs):
    """Define the details of a test in a basic suite.

    Args:
        tast_expr: The tast expression to run. Only applicable to skylab tests.
        test_level_retries: The number of times to retry tests. Only applicable
            to skylab tests.
        mixins: A list of names of mixins to apply to the test.
        remove_mixins: A list of names of mixins to skip applying to the test.
        **kwargs: The mixin values, see _mixin_values for allowed keywords and
            their meanings.

    Returns:
        An object that can be used as a value in the dict passed to the
        tests argument of targets.legacy_basic_suite.
    """
    return struct(
        tast_expr = tast_expr,
        test_level_retries = test_level_retries,
        mixins = mixins,
        remove_mixins = remove_mixins,
        mixin_values = _mixin_values(**kwargs) or None,
    )

def _legacy_compound_suite(*, name, basic_suites):
    """Define a matrix compound suite.

    //infra/config/generated/testing/test_suites.pyl will be generated from the
    declared compound suites, as well as declared basic suites (see
    _legacy_basic_suite) and declared matrix compound suites (see
    _legacy_matrix_compound_suite) to be copied to //testing/buildbot and
    consumed by //testing/buildbot/generate_buildbot_json.py.

    A compound suite is a collection of basic suites that can be specified in
    waterfalls.pyl as the test suite for one of the test types.

    Args:
        name: The name of the matrix compound suite.
        basic_suites: A list of names of basic suites to compose.
    """
    key = _LEGACY_COMPOUND_SUITE.add(name)
    graph.add_edge(keys.project(), key)

    for s in basic_suites:
        graph.add_edge(key, _LEGACY_BASIC_SUITE.key(s))

    # TODO: crbug.com/1420012 - Make compound suites usable as bundles

def _legacy_matrix_compound_suite(*, name, basic_suites):
    """Define a matrix compound suite.

    //infra/config/generated/testing/test_suites.pyl will be generated from the
    declared matrix compound suites, as well as declared basic suites (see
    _legacy_basic_suite) and declared compound suites (see
    _legacy_compound_suite) to be copied to //testing/buildbot and
    consumed by //testing/buildbot/generate_buildbot_json.py.

    A matrix compound suite is a suite that composes multiple basic suites, with
    the capability to expand the tests of basic suites with specified variants.
    A matrix compound suite can be specified in waterfalls.pyl as the test suite
    for one of the test suite types.

    Args:
        name: The name of the matrix compound suite.
        basic_suites: A dict mapping the name of a basic suite to the matrix
            config for the suite, which must be an instance returned from
            targets.legacy_matrix_config. A None value is equivalent to
            targets.legacy_matrix_config(), which will add the tests from the
            basic suite without performing any variant expansion.
    """
    key = _LEGACY_MATRIX_COMPOUND_SUITE.add(name)
    graph.add_edge(keys.project(), key)

    for basic_suite_name, config in basic_suites.items():
        # This edge won't actually be used, but it ensures that the basic suite exists
        graph.add_edge(key, _LEGACY_BASIC_SUITE.key(basic_suite_name))
        matrix_config_key = _LEGACY_MATRIX_CONFIG.add(name, basic_suite_name)
        graph.add_edge(key, matrix_config_key)
        config = config or _legacy_matrix_config()
        for m in config.mixins:
            graph.add_edge(matrix_config_key, _TARGET_MIXIN.key(m))
        for v in config.variants:
            graph.add_edge(matrix_config_key, _TARGET_VARIANT.key(v))

    # TODO: crbug.com/1420012 - Make matrix compound suites usable as bundles

def _legacy_matrix_config(*, mixins = [], variants = []):
    """Define the matrix details for a basic suite.

    Args:
        mixins: An optional list of mixins to apply to the tests of the
            corresponding basic suite.
        variants: An optional list of variants with which to expand the tests of
            the corresponding basic suite. If not provided, then the tests from
            the basic suite will be used without any variants applied.

    Returns:
        An object that can be used as a value in the dict passed to the
        basic_suites argument of targets.legacy_matrix_compound_suite.
    """
    return struct(
        mixins = mixins,
        variants = variants,
    )

targets = struct(
    # Functions for declaring binaries, which can be referred to by gtests and
    # isolated script tests
    binaries = struct(
        console_test_launcher = _console_test_launcher,
        generated_script = _generated_script,
        script = _script,
        windowed_test_launcher = _windowed_test_launcher,
    ),

    # Functions for declaring tests
    tests = struct(
        gpu_telemetry_test = _gpu_telemetry_test,
        gtest_test = _gtest_test,
        isolated_script_test = _isolated_script_test,
        junit_test = _junit_test,
        script_test = _script_test,
    ),

    # Functions for declaring compile targets
    compile_target = _compile_target,

    # Functions for declaring bundles
    bundle = _bundle,
    legacy_basic_suite = _legacy_basic_suite,
    legacy_test_config = _legacy_test_config,
    legacy_compound_suite = _legacy_compound_suite,
    legacy_matrix_compound_suite = _legacy_matrix_compound_suite,
    legacy_matrix_config = _legacy_matrix_config,
    mixin = _mixin,
    variant = _variant,
    cipd_package = _cipd_package,
    merge = _merge,
    resultdb = _resultdb,
    swarming = _swarming,
    skylab = _skylab,
)

_PYL_HEADER_FMT = """\
# THIS IS A GENERATED FILE DO NOT EDIT!!!
# Instead:
# 1. Modify {star_file}
# 2. Run //infra/config/main.star
# 3. Run //infra/config/scripts/sync-pyl-files.py

{{
{entries}
}}
"""

def _generate_gn_isolate_map_pyl(ctx):
    entries = []
    for n in graph.children(keys.project(), _TARGET_BINARY.kind, graph.KEY_ORDER):
        entries.append('  "{}": {{'.format(n.key.id))
        entries.append('    "label": "{}",'.format(n.props.label))
        if n.props.label_type != None:
            entries.append('    "label_type": "{}",'.format(n.props.label_type))
        entries.append('    "type": "{}",'.format(n.props.type))
        if n.props.executable != None:
            entries.append('    "executable": "{}",'.format(n.props.executable))
        if n.props.executable_suffix != None:
            entries.append('    "executable_suffix": "{}",'.format(n.props.executable_suffix))
        if n.props.script != None:
            entries.append('    "script": "{}",'.format(n.props.script))
        if n.props.skip_usage_check:
            entries.append('    "skip_usage_check": {},'.format(n.props.skip_usage_check))
        if n.props.args:
            entries.append('    "args": [')
            for a in n.props.args:
                entries.append('      "{}",'.format(a))
            entries.append("    ],")
        entries.append("  },")
    ctx.output["testing/gn_isolate_map.pyl"] = _PYL_HEADER_FMT.format(
        star_file = "//infra/config/targets/binaries.star and/or //infra/config/targets/tests.star (for tests defined using targets.tests.junit_test)",
        entries = "\n".join(entries),
    )

lucicfg.generator(_generate_gn_isolate_map_pyl)

def _formatter(*, indent_level = 1, indent_size = 2):
    state = dict(
        lines = [],
        indent = indent_level * indent_size,
    )

    def add_line(s):
        if s:
            state["lines"].append(" " * state["indent"] + s)
        else:
            state["lines"].append("")

    def open_scope(s):
        add_line(s)
        state["indent"] += indent_size

    def close_scope(s):
        state["indent"] -= indent_size
        add_line(s)

    def output():
        return "\n".join(state["lines"])

    return struct(
        add_line = add_line,
        open_scope = open_scope,
        close_scope = close_scope,
        output = output,
    )

def _generate_swarming_values(formatter, swarming):
    """Generate the pyl definitions for swarming fields.

    Swarming fields are the fields contained in values for the swarming,
    android_swarming and chromeos_swarming fields in mixins/variants/tests.

    Args:
      formatter: The formatter object used for generating indented
        output.
      swarming: The swarming value to generate the fields for.
    """

    def dimension_value(x):
        if x == None:
            return x
        return "'{}'".format(x)

    if swarming.enable != None:
        formatter.add_line("'can_use_on_swarming_builders': {},".format(swarming.enable))
    if swarming.shards:
        formatter.add_line("'shards': {},".format(swarming.shards))
    if swarming.dimensions:
        formatter.open_scope("'dimensions': {")
        for dim, value in swarming.dimensions.items():
            formatter.add_line("'{}': {},".format(dim, dimension_value(value)))
        formatter.close_scope("},")
    if swarming.optional_dimensions:
        formatter.open_scope("'optional_dimensions': {")
        for timeout, dimensions in swarming.optional_dimensions.items():
            formatter.open_scope("'{}': {{".format(timeout))
            for dim, value in dimensions.items():
                formatter.add_line("'{}': {},".format(dim, dimension_value(value)))
            formatter.close_scope("},")
        formatter.close_scope("},")
    if swarming.containment_type:
        formatter.add_line("'containment_type': '{}',".format(swarming.containment_type))
    if swarming.cipd_packages:
        formatter.open_scope("'cipd_packages': [")
        for package in swarming.cipd_packages:
            formatter.open_scope("{")
            formatter.add_line("'cipd_package': '{}',".format(package.package))
            formatter.add_line("'location': '{}',".format(package.location))
            formatter.add_line("'revision': '{}',".format(package.revision))
            formatter.close_scope("},")
        formatter.close_scope("],")
    if swarming.expiration_sec:
        formatter.add_line("'expiration': {},".format(swarming.expiration_sec))
    if swarming.hard_timeout_sec:
        formatter.add_line("'hard_timeout': {},".format(swarming.hard_timeout_sec))
    if swarming.io_timeout_sec:
        formatter.add_line("'io_timeout': {},".format(swarming.io_timeout_sec))
    if swarming.idempotent != None:
        formatter.add_line("'idempotent': {},".format(swarming.idempotent))
    if swarming.named_caches:
        formatter.open_scope("'named_caches': [")
        for cache in swarming.named_caches:
            formatter.open_scope("{")
            formatter.add_line("'name': '{}',".format(cache.name))
            formatter.add_line("'path': '{}',".format(cache.path))
            formatter.close_scope("},")
        formatter.close_scope("],")
    if swarming.service_account:
        formatter.add_line("'service_account': '{}',".format(swarming.service_account))

def _generate_mixin_values(formatter, mixin, generate_skylab_container = False):
    """Generate the pyl definitions for mixin fields.

    Mixin fields are fields that are common to mixins, variants and test
    definitions within basic suites.

    Args:
      formatter: The formatter object used for generating indented
        output.
      mixin: Dict containing the mixin values to output.
      generate_skylab_container: Whether or not to generate the skylab
        key to contain the fields of the skylab value. Mixins and the
        generated test have those fields at top-level, but variants have
        them under a skylab key.
    """
    if "description" in mixin:
        formatter.add_line("'description': '{}',".format(mixin["description"]))

    for args_attr in (
        "args",
        "precommit_args",
        "android_args",
        "chromeos_args",
        "desktop_args",
        "lacros_args",
        "linux_args",
        "mac_args",
        "win_args",
        "win64_args",
    ):
        if args_attr in mixin:
            formatter.open_scope("'{}': [".format(args_attr))
            for a in mixin[args_attr]:
                formatter.add_line("'{}',".format(a))
            formatter.close_scope("],")

    if "check_flakiness_for_new_tests" in mixin:
        formatter.add_line("'check_flakiness_for_new_tests': {},".format(mixin["check_flakiness_for_new_tests"]))

    if "ci_only" in mixin:
        formatter.add_line("'ci_only': {},".format(mixin["ci_only"]))

    if "isolate_profile_data" in mixin:
        formatter.add_line("'isolate_profile_data': {},".format(mixin["isolate_profile_data"]))

    if "timeout_sec" in mixin:
        formatter.add_line("'timeout_sec': {},".format(mixin["timeout_sec"]))

    for swarming_attr in ("swarming", "android_swarming", "chromeos_swarming"):
        if swarming_attr in mixin:
            swarming = mixin[swarming_attr]
            formatter.open_scope("'{}': {{".format(swarming_attr))
            _generate_swarming_values(formatter, swarming)
            formatter.close_scope("},")

    if "merge" in mixin:
        merge = mixin["merge"]
        formatter.open_scope("'merge': {")
        formatter.add_line("'script': '{}',".format(merge.script))
        if merge.args:
            formatter.open_scope("'args': [")
            for a in merge.args:
                formatter.add_line("'{}',".format(a))
            formatter.close_scope("],")
        formatter.close_scope("},")

    if "skylab" in mixin:
        skylab = mixin["skylab"]
        if generate_skylab_container:
            formatter.open_scope("'skylab': {")
        formatter.add_line("'cros_board': '{}',".format(skylab.cros_board))
        if skylab.cros_model:
            formatter.add_line("'cros_model': '{}',".format(skylab.cros_model))
        if skylab.cros_img:
            formatter.add_line("'cros_img': '{}',".format(skylab.cros_img))
        if skylab.use_lkgm:
            formatter.add_line("'use_lkgm': True,")
        if skylab.autotest_name:
            formatter.add_line("'autotest_name': '{}',".format(skylab.autotest_name))
        if skylab.bucket:
            formatter.add_line("'bucket': '{}',".format(skylab.bucket))
        if skylab.dut_pool:
            formatter.add_line("'dut_pool': '{}',".format(skylab.dut_pool))
        if skylab.public_builder:
            formatter.add_line("'public_builder': '{}',".format(skylab.public_builder))
        if skylab.public_builder_bucket:
            formatter.add_line("'public_builder_bucket': '{}',".format(skylab.public_builder_bucket))
        if skylab.shards:
            formatter.add_line("'shards': {},".format(skylab.shards))
        if generate_skylab_container:
            formatter.close_scope("},")

    if "resultdb" in mixin:
        resultdb = mixin["resultdb"]
        formatter.open_scope("'resultdb': {")
        if resultdb.enable:
            formatter.add_line("'enable': True,")
        if resultdb.has_native_resultdb_integration:
            formatter.add_line("'has_native_resultdb_integration': True,")
        if resultdb.result_format != None:
            formatter.add_line("'result_format': '{}',".format(resultdb.result_format))
        if resultdb.result_file != None:
            formatter.add_line("'result_file': '{}',".format(resultdb.result_file))
        formatter.close_scope("},")

    if "use_isolated_scripts_api" in mixin:
        formatter.add_line("'use_isolated_scripts_api': {},".format(mixin["use_isolated_scripts_api"]))

    if "shards" in mixin:
        formatter.add_line("'shards': {},".format(mixin["shards"]))

    if "experiment_percentage" in mixin:
        formatter.add_line("'experiment_percentage': {},".format(mixin["experiment_percentage"]))

def _generate_mixins_pyl(ctx):
    formatter = _formatter()

    for n in graph.children(keys.project(), _TARGET_MIXIN.kind, graph.KEY_ORDER):
        mixin = n.props.mixin_values
        formatter.open_scope("'{}': {{".format(n.key.id))

        _generate_mixin_values(formatter, mixin)

        formatter.close_scope("},")

    ctx.output["testing/mixins.pyl"] = _PYL_HEADER_FMT.format(
        star_file = "//infra/config/targets/mixins.star",
        entries = formatter.output(),
    )

lucicfg.generator(_generate_mixins_pyl)

def _generate_variants_pyl(ctx):
    formatter = _formatter()

    for n in graph.children(keys.project(), _TARGET_VARIANT.kind, graph.KEY_ORDER):
        mixin = n.props.mixin_values
        formatter.open_scope("'{}': {{".format(n.key.id))

        formatter.add_line("'identifier': '{}',".format(n.props.identifier))

        if not n.props.enabled:
            formatter.add_line("'enabled': {},".format(n.props.enabled))

        _generate_mixin_values(formatter, mixin, generate_skylab_container = True)

        if n.props.mixins:
            formatter.open_scope("'mixins': [")
            for m in n.props.mixins:
                formatter.add_line("'{}',".format(m))
            formatter.close_scope("],")

        formatter.close_scope("},")

    ctx.output["testing/variants.pyl"] = _PYL_HEADER_FMT.format(
        star_file = "//infra/config/targets/variants.star",
        entries = formatter.output(),
    )

lucicfg.generator(_generate_variants_pyl)

def _is_empty(s):
    for a in dir(s):
        if getattr(s, a) != None:
            return False
    return True

def _generate_test_suites_pyl(ctx):
    formatter = _formatter()

    formatter.open_scope("'basic_suites': {")

    for suite in graph.children(keys.project(), _LEGACY_BASIC_SUITE.kind, graph.KEY_ORDER):
        formatter.add_line("")
        formatter.open_scope("'{}': {{".format(suite.key.id))

        for test_node in graph.children(suite.key, _LEGACY_TEST.kind, graph.KEY_ORDER):
            test_name = test_node.key.id

            binary_nodes = graph.children(test_node.key, _TARGET_BINARY.kind)
            if len(binary_nodes) > 1:
                fail("internal error: test {} has more than 1 binary: {}", test_node, binary_nodes)
            binary_test_config = None
            if binary_nodes:
                binary_test_config = binary_nodes[0].props.test_config
            binary_test_config = binary_test_config or _binary_test_config()

            target_test_config = test_node.props.basic_suite_test_config
            suite_test_config = suite.props.tests[test_node.key.id]

            if _is_empty(binary_test_config) and _is_empty(target_test_config) and _is_empty(suite_test_config):
                formatter.add_line("'{}': {{}},".format(test_name))
                continue

            formatter.open_scope("'{}': {{".format(test_name))

            if target_test_config.script:
                formatter.add_line("'script': '{}',".format(target_test_config.script))

            # This is intentionally transforming binary -> test to remain
            # backwards-compatible with //testing/buildbot
            if target_test_config.binary:
                formatter.add_line("'test': '{}',".format(target_test_config.binary))
            if binary_test_config.results_handler:
                formatter.add_line("'results_handler': '{}',".format(binary_test_config.results_handler))

            if target_test_config.telemetry_test_name:
                formatter.add_line("'telemetry_test_name': '{}',".format(target_test_config.telemetry_test_name))

            if suite_test_config.tast_expr:
                formatter.add_line("'tast_expr': '{}',".format(suite_test_config.tast_expr))
            if suite_test_config.test_level_retries:
                formatter.add_line("'test_level_retries': {},".format(suite_test_config.test_level_retries))

            mixins = args.listify(target_test_config.mixins, suite_test_config.mixins)
            if mixins:
                formatter.open_scope("'mixins': [")
                for m in mixins:
                    formatter.add_line("'{}',".format(m))
                formatter.close_scope("],")
            if suite_test_config.remove_mixins:
                formatter.open_scope("'remove_mixins': [")
                for m in suite_test_config.remove_mixins:
                    formatter.add_line("'{}',".format(m))
                formatter.close_scope("],")

            mixin_values = dict(suite_test_config.mixin_values or {})

            # Merge any args from the target with those specified for the test
            # in the suite
            merged_args = args.listify(target_test_config.args, mixin_values.get("args"))
            if merged_args:
                mixin_values["args"] = merged_args

            # merge and resultdb can be set on the binary, but don't override
            # values set on the test in the suite
            for a in ("merge", "resultdb"):
                value = getattr(binary_test_config, a)
                if value:
                    mixin_values.setdefault(a, value)
            _generate_mixin_values(formatter, mixin_values)

            formatter.close_scope("},")

        formatter.close_scope("},")

    formatter.close_scope("},")

    formatter.add_line("")

    formatter.open_scope("'compound_suites': {")

    for suite in graph.children(keys.project(), _LEGACY_COMPOUND_SUITE.kind, graph.KEY_ORDER):
        formatter.add_line("")
        formatter.open_scope("'{}': [".format(suite.key.id))
        for basic_suite in graph.children(suite.key, _LEGACY_BASIC_SUITE.kind, graph.KEY_ORDER):
            formatter.add_line("'{}',".format(basic_suite.key.id))
        formatter.close_scope("],")

    formatter.close_scope("},")

    formatter.add_line("")

    formatter.open_scope("'matrix_compound_suites': {")

    for suite in graph.children(keys.project(), _LEGACY_MATRIX_COMPOUND_SUITE.kind, graph.KEY_ORDER):
        formatter.add_line("")
        formatter.open_scope("'{}': {{".format(suite.key.id))
        for matrix_config in graph.children(suite.key, _LEGACY_MATRIX_CONFIG.kind, graph.KEY_ORDER):
            # The order that mixins are declared is significant,
            # DEFINITION_ORDER preserves the order that the edges were added
            # from the parent to the child
            mixins = graph.children(matrix_config.key, _TARGET_MIXIN.kind, graph.DEFINITION_ORDER)
            variants = graph.children(matrix_config.key, _TARGET_VARIANT.kind, graph.KEY_ORDER)
            if not (mixins or variants):
                formatter.add_line("'{}': {{}},".format(matrix_config.key.id))
                continue
            formatter.open_scope("'{}': {{".format(matrix_config.key.id))
            if mixins:
                formatter.open_scope("'mixins': [")
                for m in mixins:
                    formatter.add_line("'{}',".format(m.key.id))
                formatter.close_scope("],")
            if variants:
                formatter.open_scope("'variants': [")
                for v in variants:
                    formatter.add_line("'{}',".format(v.key.id))
                formatter.close_scope("],")
            formatter.close_scope("},")
        formatter.close_scope("},")

    formatter.close_scope("},")

    ctx.output["testing/test_suites.pyl"] = _PYL_HEADER_FMT.format(
        star_file = "//infra/config/targets/basic_suites.star, //infra/config/targets/compound_suites.star and/or //infra/config/targets/matrix_compound_suites.star",
        entries = formatter.output(),
    )

lucicfg.generator(_generate_test_suites_pyl)
