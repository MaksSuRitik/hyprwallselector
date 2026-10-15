param(
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$InnoCompiler,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version = '0.3.1',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$env:PATH = "$QtRoot\bin;$env:PATH"
function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with $LASTEXITCODE" }
}
if (-not $SkipBuild) {
    Run-Checked "$QtRoot\bin\cmake.exe" @('-S',$sourceRoot,'-B',$BuildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON',"-DHWS_VERSION=$Version", "-DCMAKE_PREFIX_PATH=$QtRoot")
    Run-Checked "$QtRoot\bin\cmake.exe" @('--build',$BuildRoot,'-j','4')
    Copy-Item -LiteralPath "$QtRoot\bin\ffmpeg.exe", "$QtRoot\bin\ffprobe.exe" -Destination $BuildRoot
    Run-Checked "$QtRoot\bin\ctest.exe" @('--test-dir',$BuildRoot,'--output-on-failure')
}
# A fresh directory prevents old DLLs being carried into a new release.
$bundle = Join-Path $OutputRoot ('Hyprwallselector-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $bundle -Force | Out-Null
Copy-Item -LiteralPath "$BuildRoot\hyprwallselector.exe", "$QtRoot\bin\ffmpeg.exe", "$QtRoot\bin\ffprobe.exe" -Destination $bundle
Run-Checked "$QtRoot\bin\windeployqt6.exe" @('--release','--no-translations','--no-system-d3d-compiler','--no-opengl-sw','--qmldir',"$sourceRoot\qml", "$bundle\hyprwallselector.exe")
"[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`n" | Set-Content -LiteralPath "$bundle\qt.conf" -Encoding utf8
$queue = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $bundle -Recurse -File | Where-Object Extension -In '.exe','.dll' | ForEach-Object { $queue.Enqueue($_.FullName) }
$manifest = [Collections.Generic.List[object]]::new()
while ($queue.Count) {
    $file = $queue.Dequeue()
    if (-not $seen.Add($file)) { continue }
    $headers = & "$QtRoot\bin\objdump.exe" -p $file
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $file" }
    foreach ($line in $headers) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $name = $Matches[1]
        $dependency = Join-Path "$QtRoot\bin" $name
        if (Test-Path -LiteralPath $dependency) {
            $target = Join-Path $bundle $name
            if (-not (Test-Path -LiteralPath $target)) {
                Copy-Item -LiteralPath $dependency -Destination $target
                $queue.Enqueue($target)
                $manifest.Add([pscustomobject]@{File=$name; SHA256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash})
            }
        } elseif ($name -notmatch '^(api|ext)-ms-' -and -not (Test-Path -LiteralPath "$env:SystemRoot\System32\$name")) {
            throw "Unresolved dependency: $name (required by $file)"
        }
    }
}
Copy-Item -LiteralPath "$sourceRoot\LICENSE", "$sourceRoot\docs\WINDOWS.md" -Destination $bundle
$licenseRoot = Join-Path $bundle 'licenses'
New-Item -ItemType Directory -Path $licenseRoot | Out-Null
Copy-Item -LiteralPath "$QtRoot\share\licenses" -Destination "$licenseRoot\msys2" -Recurse
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$bundle\dependencies.json" -Encoding utf8
@'
This distribution includes dynamically linked Qt, FFmpeg and their dependencies.
See licenses/ for the individual copyright notices and license texts.
Build dependencies and source recipes: https://github.com/msys2/MINGW-packages
Package information: https://packages.msys2.org
Hyprwallselector source and build scripts are included in source.zip.
'@ | Set-Content -LiteralPath "$bundle\THIRD-PARTY.txt" -Encoding utf8
$sourcePaths = 'CMakeLists.txt','LICENSE','README.md','src','qml','shaders','packaging','tests','docs','.github' | ForEach-Object { Join-Path $sourceRoot $_ }
Compress-Archive -LiteralPath $sourcePaths -DestinationPath "$bundle\source.zip" -CompressionLevel Optimal
if ($InnoCompiler) {
    Run-Checked $InnoCompiler @("/DBundleDir=$bundle", "/DOutputDir=$OutputRoot", "/DAppVersion=$Version", "$PSScriptRoot\installer.iss")
    $installer = Join-Path $OutputRoot "Hyprwallselector-$Version-Setup.exe"
    ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLower() + '  ' + [IO.Path]::GetFileName($installer)) | Set-Content -LiteralPath "$OutputRoot\SHA256SUMS" -Encoding ascii
}
Write-Output "Bundle: $bundle"
