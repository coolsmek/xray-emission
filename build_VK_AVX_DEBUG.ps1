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
Write-Host "Building Vulkan game executable with VERIFIED (Debug) renderer..."
Write-Host "   xrEngine      > ReleaseVK-AVX|x64  (AnomalyVKAVX.exe, optimized)"
Write-Host "   xrRenderVK    > VerifiedVK|x64     (debug renderer, VK_ENABLE_TESTS active)"
Write-Host "   All other libs > Release-AVX|x64"
Write-Host ""

$buildFailed = $false

$slnPath = "src\engine-vs2022.sln"
$slnAbs = (Resolve-Path $slnPath).Path

Write-Host "Building full Vulkan-AVX solution (with VerifiedVK renderer) using native Vulkan-AVX-Debug config..." -ForegroundColor Cyan
& $MSBuild $slnAbs /p:Configuration="Vulkan-AVX-Debug" /p:Platform=x64 /m /v:minimal 2>&1 | ForEach-Object {
    $line = $_.ToString()
    if ($line -match 'error\s[A-Z]\d+') {
        Write-Host $line -ForegroundColor Red
        $buildFailed = $true
    }
    elseif ($line -match 'warning\s[A-Z]\d+') {
        Write-Host $line -ForegroundColor Yellow
    }
    else {
        Write-Host $line
    }
}


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

    $outputDir = "_build\_game\bin_dbg\x64\ReleaseVK-AVX"
    $sourceExe = "$outputDir\AnomalyVKAVX.exe"

    Write-Host "Output exe:  $sourceExe"

    if (-not (Test-Path $sourceExe)) {
        Write-Host "Warning: Compiled executable not found at expected path: $sourceExe" -ForegroundColor Yellow
    }

    # Ensure shaderc_shared.dll is present
    $destPath = Join-Path (Resolve-Path $outputDir) "shaderc_shared.dll"
    if (Test-Path $destPath) {
        Write-Host "shaderc_shared.dll already present in $outputDir"
    }
    else {
        $shadercPath = $null

        # 1. Check Vulkan SDK environment variables
        if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK "Bin\shaderc_shared.dll"))) {
            $shadercPath = Join-Path $env:VULKAN_SDK "Bin\shaderc_shared.dll"
        }
        elseif ($env:VK_SDK_PATH -and (Test-Path (Join-Path $env:VK_SDK_PATH "Bin\shaderc_shared.dll"))) {
            $shadercPath = Join-Path $env:VK_SDK_PATH "Bin\shaderc_shared.dll"
        }
        # 2. Check local repo / _build tree
        else {
            $shaderc = Get-ChildItem -Path "_build", "sdk" -Filter "shaderc_shared.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($shaderc) {
                $shadercPath = $shaderc.FullName
            }
        }

        if ($shadercPath) {
            Write-Host "Copying shaderc_shared.dll from $shadercPath to $outputDir..."
            Copy-Item -Path $shadercPath -Destination $outputDir -Force
        }
        else {
            Write-Host "Warning: shaderc_shared.dll not found in Vulkan SDK ($env:VULKAN_SDK) or repository." -ForegroundColor Yellow
        }
    }

    Exit 0
}
