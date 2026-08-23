$ErrorActionPreference = "Stop"

Write-Host "Locating MSBuild..."
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    Write-Host "Error: vswhere.exe not found. Visual Studio installer is missing." -ForegroundColor Red
    Exit 1
}

$MSBuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
if (-not $MSBuild) {
    Write-Host "Error: MSBuild.exe could not be found via vswhere." -ForegroundColor Red
    Exit 1
}

Write-Host "Found MSBuild at: $MSBuild"
Write-Host ""
Write-Host "Building full DX11-AVX game executable..."
Write-Host "   xrEngine      > ReleaseR4-AVX|x64  (AnomalyDX11AVX.exe, optimized)"
Write-Host "   xrRender_R4    > Release-AVX|x64"
Write-Host "   All other libs > Release-AVX|x64"
Write-Host ""

# Run MSBuild and track if any errors occur
$buildFailed = $false

& $MSBuild src\engine-vs2022.sln /p:Configuration="DX11-AVX" /p:Platform=x64 /m /v:minimal 2>&1 | ForEach-Object {
    $line = $_.ToString()
    if ($line -match 'error\s[A-Z]\d+') {
        Write-Host $line -ForegroundColor Red
        $buildFailed = $true
    } elseif ($line -match 'warning\s[A-Z]\d+') {
        Write-Host $line -ForegroundColor Yellow
    } else {
        Write-Host $line
    }
}

# Check if MSBuild failed or if our filter caught an error string
if ($LASTEXITCODE -ne 0 -or $buildFailed) {
    Write-Host ""
    Write-Host "----------------------------------------" -ForegroundColor Red
    Write-Host "BUILD FAILED" -ForegroundColor Red
    Write-Host "----------------------------------------" -ForegroundColor Red
    Exit 1
}
else {
    Write-Host ""
    Write-Host "----------------------------------------" -ForegroundColor Green
    Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
    Write-Host "----------------------------------------" -ForegroundColor Green
    Write-Host ""

    $outputDir = "_build\_game\bin_dbg"
    $sourceExe = "$outputDir\AnomalyDX11AVX.exe"

    Write-Host "Output exe:  $sourceExe"

    if (-not (Test-Path $sourceExe)) {
        Write-Host "Warning: Compiled executable not found at expected path: $sourceExe" -ForegroundColor Yellow
    }

    Exit 0
}