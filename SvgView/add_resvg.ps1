
# add_resvg.ps1 — integrates resvg C API into SvgView (Win32/MinGW)
# Run from: C:\Users\NalleBerg\Documents\C++\Workspace\NSBEdit\SvgView

param(
    [string]$ResvgSrc = "resvg-0.48.1",
    [string]$TargetDir = "."
)

$ErrorActionPreference = "Stop"

Write-Host "=== Adding resvg C API to SvgView ===" -ForegroundColor Cyan

# 1. Validate source exists
if (-not (Test-Path "$ResvgSrc\crates\c-api")) {
    Write-Error "Cannot find $ResvgSrc\crates\c-api"
    exit 1
}

# 2. Copy resvg.h header
Write-Host "Copying resvg.h..." -ForegroundColor Yellow
Copy-Item "$ResvgSrc\crates\c-api\resvg.h" "$TargetDir\resvg.h" -Force



# 4. Update nanosvg_impl.cpp — remains as fallback, no changes needed
Write-Host "Updating nanosvg_impl.cpp (no changes needed)..." -ForegroundColor Yellow

# 5. Update SvgView.h — add resvg header and new API functions
Write-Host "Updating SvgView.h..." -ForegroundColor Yellow

# Read current file
$svgViewH = Get-Content "$TargetDir\SvgView.h" -Raw

# Check if resvg is already added
if ($svgViewH -notmatch '#include "resvg.h"') {
    # Add include after existing includes
    $svgViewH = $svgViewH -replace '(#include "nanosvg\.h")', "`$1`r`n#include `"resvg.h`""
}

# Add new function declarations if not present
if ($svgViewH -notmatch 'int LoadSvgResvg\(') {
    $newDeclarations = @"

// --- resvg C API integration ---
// Returns 0 on success, -1 on error.
int LoadSvgResvg(const char* filepath);
int LoadSvgResvgFromMemory(const char* data, int len);
void* GetResvgPixels(void);
int GetResvgWidth(void);
int GetResvgHeight(void);
void FreeResvg(void);
"@
    # Insert before the last closing brace or at end
    if ($svgViewH -match '(\}\s*//\s*class\s+SvgView\b)') {
        $svgViewH = $svgViewH -replace '\}\s*//\s*class\s+SvgView\b', "$newDeclarations`r`n} // class SvgView"
    } else {
        $svgViewH += $newDeclarations
    }
}

Set-Content "$TargetDir\SvgView.h" $svgViewH -NoNewline
Write-Host "  Done." -ForegroundColor Green

# 6. Update SvgView.cpp — add resvg rendering functions
Write-Host "Updating SvgView.cpp with resvg renderer..." -ForegroundColor Yellow

$svgViewCpp = Get-Content "$TargetDir\SvgView.cpp" -Raw

if ($svgViewCpp -notmatch 'resvg_options_create') {
    # Add implementation block before the existing nanosvg code
    $resvgImpl = @"

// ==================== resvg C API integration ====================
static resvg_options* g_opt = NULL;
static resvg_render_tree* g_tree = NULL;
static unsigned char* g_pixels = NULL;
static int g_width = 0;
static int g_height = 0;

int LoadSvgResvg(const char* filepath)
{
    // First cleanup any previous render
    FreeResvg();
    
    if (!g_opt) {
        g_opt = resvg_options_create();
        resvg_options_set_dpi(g_opt, 96.0f);
        resvg_options_set_font_family(g_opt, "Arial");
        resvg_options_set_font_size(g_opt, 16.0f);
        // Load system fonts only once
        resvg_options_load_system_fonts(g_opt);
    }
    
    int err = resvg_parse_tree_from_file(filepath, g_opt, &g_tree);
    if (err != RESVG_OK) {
        return -1;
    }
    
    if (resvg_is_image_empty(g_tree)) {
        return -2;
    }
    
    resvg_size size = resvg_get_image_size(g_tree);
    g_width = (int)size.width;
    g_height = (int)size.height;
    
    if (g_width <= 0 || g_height <= 0) {
        resvg_tree_destroy(g_tree);
        g_tree = NULL;
        return -3;
    }
    
    // Allocate pixel buffer (RGBA)
    int stride = g_width * 4;
    g_pixels = (unsigned char*)malloc(stride * g_height);
    if (!g_pixels) {
        resvg_tree_destroy(g_tree);
        g_tree = NULL;
        return -4;
    }
    memset(g_pixels, 0, stride * g_height);
    
    // Render
    resvg_transform identity = resvg_transform_identity();
    resvg_render(g_tree, identity, size, g_pixels);
    
    return 0;
}

int LoadSvgResvgFromMemory(const char* data, int len)
{
    FreeResvg();
    
    if (!g_opt) {
        g_opt = resvg_options_create();
        resvg_options_set_dpi(g_opt, 96.0f);
        resvg_options_set_font_family(g_opt, "Arial");
        resvg_options_set_font_size(g_opt, 16.0f);
        resvg_options_load_system_fonts(g_opt);
    }
    
    int err = resvg_parse_tree_from_data(data, (uintptr_t)len, g_opt, &g_tree);
    if (err != RESVG_OK) return -1;
    if (resvg_is_image_empty(g_tree)) return -2;
    
    resvg_size size = resvg_get_image_size(g_tree);
    g_width = (int)size.width;
    g_height = (int)size.height;
    if (g_width <= 0 || g_height <= 0) { resvg_tree_destroy(g_tree); g_tree = NULL; return -3; }
    
    int stride = g_width * 4;
    g_pixels = (unsigned char*)malloc(stride * g_height);
    if (!g_pixels) { resvg_tree_destroy(g_tree); g_tree = NULL; return -4; }
    memset(g_pixels, 0, stride * g_height);
    
    resvg_transform identity = resvg_transform_identity();
    resvg_render(g_tree, identity, size, g_pixels);
    
    return 0;
}

void* GetResvgPixels(void) { return g_pixels; }
int GetResvgWidth(void) { return g_width; }
int GetResvgHeight(void) { return g_height; }

void FreeResvg(void)
{
    if (g_pixels) { free(g_pixels); g_pixels = NULL; }
    if (g_tree) { resvg_tree_destroy(g_tree); g_tree = NULL; }
    // Keep g_opt for reuse
}

// ==================== End resvg integration ====================

"@
    # Insert after includes and before any function definitions
    $svgViewCpp = $resvgImpl + $svgViewCpp
}

Set-Content "$TargetDir\SvgView.cpp" $svgViewCpp -NoNewline
Write-Host "  Done." -ForegroundColor Green

# 7. Update makeit.bat with new link flags
Write-Host "Updating makeit.bat..." -ForegroundColor Yellow
$makeitBat = Get-Content "$TargetDir\makeit.bat" -Raw
if ($makeitBat -notmatch 'libresvg') {
    $makeitBat = $makeitBat -replace 'g\+\+ nanosvg_impl\.obj SvgView\.obj -o SvgView\.exe',
                                    'g++ nanosvg_impl.obj SvgView.obj libresvg.a -o SvgView.exe'
    $makeitBat = $makeitBat -replace '-lgdiplus -lcomctl32 -lshlwapi -mwindows -O2',
                                    '-lgdiplus -lcomctl32 -lshlwapi -lusp10 -lole32 -lws2_32 -mwindows -O2'
    Set-Content "$TargetDir\makeit.bat" $makeitBat -NoNewline
}
Write-Host "  Done." -ForegroundColor Green

# 8. Also copy the ResvgQt.h for reference (not required for Win32)
Write-Host "Copying ResvgQt.h for reference..." -ForegroundColor Yellow
Copy-Item "$ResvgSrc\crates\c-api\ResvgQt.h" "$TargetDir\ResvgQt.h" -Force

Write-Host ""
Write-Host "=== Integration complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "1. In SvgView.cpp, replace the nanosvg rendering loop with calls to:"
Write-Host "   LoadSvgResvg(filename) then iterate over GetResvgPixels()/GetResvgWidth()/GetResvgHeight()"
Write-Host "2. Rebuild: run makeit.bat (it now links libresvg.a)"
Write-Host ""
Write-Host "Note: The resvg library (libresvg.a) is ~5MB. Make sure it's in the same dir as .exe"