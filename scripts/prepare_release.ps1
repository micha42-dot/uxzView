param([switch]$BootstrapSDK)
$ErrorActionPreference = 'Stop'
$rawRequirementBefore = $env:IX_REQUIRE_RAW
$toolsRequirementBefore = $env:IX_REQUIRE_TOOLS
$pathBefore = $env:PATH
$project = Split-Path -Parent $PSScriptRoot
Push-Location $project
try {
    python -m unittest discover -s tests -p "test_*.py" -v
    if ($LASTEXITCODE -ne 0) { throw 'SDK-Installer-Tests fehlgeschlagen' }
    if ($BootstrapSDK) {
        python scripts/bootstrap_sdk.py
        if ($LASTEXITCODE -ne 0) { throw 'SDK-Bootstrap fehlgeschlagen' }
    }
    python scripts/fetch_quality_fixtures.py
    if ($LASTEXITCODE -ne 0) { throw 'RAW-Testfixtures konnten nicht geprüft werden' }
    $env:IX_REQUIRE_RAW = '1'
    $env:IX_REQUIRE_TOOLS = '1'
    $sdkBin = Join-Path $project '.deps/sdk/ucrt64/bin'
    $env:PATH = "$sdkBin;$env:PATH"
    python scripts/build_tool_packs.py
    if ($LASTEXITCODE -ne 0) { throw 'Optionale Werkzeugpakete konnten nicht gebaut werden' }
    & ./scripts/build.ps1 -Configuration Release -Test
    if ($LASTEXITCODE -ne 0) { throw 'Build/Tests fehlgeschlagen' }
    foreach ($script in @('format_matrix.py', 'deploy.py', 'verify_portable.py')) {
        python "scripts/$script"
        if ($LASTEXITCODE -ne 0) { throw "$script fehlgeschlagen" }
    }
    python scripts/dependency_sources.py --check-urls
    if ($LASTEXITCODE -ne 0) { throw 'Quellpaket-Zuordnung fehlgeschlagen' }
    python scripts/package.py --release
    if ($LASTEXITCODE -ne 0) { throw 'Release-Paketierung fehlgeschlagen' }
} finally {
    $env:PATH = $pathBefore
    if ($null -eq $toolsRequirementBefore) { Remove-Item Env:IX_REQUIRE_TOOLS -ErrorAction SilentlyContinue }
    else { $env:IX_REQUIRE_TOOLS = $toolsRequirementBefore }
    if ($null -eq $rawRequirementBefore) { Remove-Item Env:IX_REQUIRE_RAW -ErrorAction SilentlyContinue }
    else { $env:IX_REQUIRE_RAW = $rawRequirementBefore }
    Pop-Location
}
