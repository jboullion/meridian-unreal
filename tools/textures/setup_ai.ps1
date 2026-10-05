# One-time setup of the AI texture tools used by tools/textures/make_placeholders.py (docs/adr/0003,
# "AI relief maps"). Everything goes under build/texai/ (git-ignored); the Marigold model (~1.7 GB)
# downloads into the Hugging Face cache on first use. Needs uv (https://docs.astral.sh/uv/) and an
# NVIDIA GPU (CUDA 12.8 build of PyTorch).
#
#   powershell -File tools/textures/setup_ai.ps1
#
# Without these, make_placeholders.py falls back to Lanczos upscaling and rule-based height maps.
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$texai = Join-Path $repo "build\texai"
New-Item -ItemType Directory -Force $texai | Out-Null

# Real-ESRGAN (base colour upscaling), portable Vulkan build, BSD-3
$esrgan = Join-Path $texai "realesrgan"
if (-not (Test-Path (Join-Path $esrgan "realesrgan-ncnn-vulkan.exe"))) {
    $zip = Join-Path $texai "realesrgan.zip"
    Invoke-WebRequest -UseBasicParsing -OutFile $zip `
        "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip"
    Expand-Archive -Force $zip $esrgan
    Remove-Item $zip
}

# Python environment for Marigold (and the comparison models in ai_maps.py)
$venv = Join-Path $texai ".venv"
$py = Join-Path $venv "Scripts\python.exe"
if (-not (Test-Path $py)) { uv venv --python 3.12 $venv }
uv pip install --python $py torch torchvision --index-url https://download.pytorch.org/whl/cu128
uv pip install --python $py diffusers transformers accelerate onnxruntime opencv-python-headless numpy scipy pillow huggingface_hub spandrel
& $py -c "import torch; print('torch', torch.__version__, 'cuda', torch.cuda.is_available())"

# Base colour upscaler (make_placeholders.py ESRGAN_MODEL): Phips' 4xTextures_GTAV_rgt-s_dither,
# CC-BY-4.0 (https://huggingface.co/Phips/4xTextures_GTAV_rgt-s_dither), 136 MB, run through spandrel.
# Other openmodeldb models can be dropped into build/texai/models/ as <name>.safetensors to compare
# (tools/lookdev/upscaler_test.ps1).
$models = Join-Path $texai "models"
New-Item -ItemType Directory -Force $models | Out-Null
$upscaler = Join-Path $models "4xTextures_GTAV_rgt-s_dither.safetensors"
if (-not (Test-Path $upscaler)) {
    Invoke-WebRequest -UseBasicParsing -OutFile $upscaler `
        "https://huggingface.co/Phips/4xTextures_GTAV_rgt-s_dither/resolve/main/4xTextures_GTAV_rgt-s_dither.safetensors"
}

# DeepBump (only for ai_maps.py's comparison test)
$deepbump = Join-Path $texai "DeepBump"
if (-not (Test-Path $deepbump)) { git clone --depth 1 https://github.com/HugoTini/DeepBump.git $deepbump }
Write-Host "AI texture tools ready; run python tools/textures/make_placeholders.py"
