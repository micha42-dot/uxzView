param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Test,[switch]$RequireFullFeatures)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$sdk=Join-Path $project '.deps/sdk/ucrt64'
if(Test-Path -LiteralPath "$sdk/bin/cmake.exe") { $env:PATH="$sdk/bin;$env:PATH"; $prefix=@("-DCMAKE_PREFIX_PATH=$sdk") } else { $prefix=@() }
Push-Location $project
try {
 $fullFeatures=if($RequireFullFeatures){'ON'}else{'OFF'}
 cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON "-DIX_REQUIRE_FULL_FEATURES=$fullFeatures" @prefix
 if($LASTEXITCODE -ne 0){throw 'CMake-Konfiguration fehlgeschlagen'}
 cmake --build build --parallel 4
 if($LASTEXITCODE -ne 0){throw 'Build fehlgeschlagen'}
 if($Test){ctest --test-dir build --output-on-failure;if($LASTEXITCODE -ne 0){python scripts/report_test_logs.py --build-dir build;throw 'Tests fehlgeschlagen'}}
} finally { Pop-Location }
