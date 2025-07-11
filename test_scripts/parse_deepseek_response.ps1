param(
    [Parameter(Mandatory=$true)]
    [string]$ClipboardFile = "clipboard_content.txt"
)

# Read the clipboard content from file (since we can't pass huge strings directly)
$content = Get-Content -Path $ClipboardFile -Raw -ErrorAction SilentlyContinue

if (-not $content) {
    Write-Host "ERROR: Could not read clipboard content from $ClipboardFile"
    exit 3
}

# Simple check if still generating - look for common indicators
$stillGenerating = $false

# Check for typing indicators
if ($content -match '\.\.\.|●●●|█|▌|_$|\|$') {
    $stillGenerating = $true
}

# Check for common "generating" words (case insensitive)
if ($content -match '(?i)(generating|thinking|typing|processing|loading)') {
    $stillGenerating = $true
}

# Check if response seems complete by looking for the prompt and response
$hasPrompt = $content -match '(?i)what is the best template'
$responseLength = $content.Length

# If we have the prompt and reasonable content length, check for response
if ($hasPrompt -and $responseLength -gt 500 -and -not $stillGenerating) {
    # Try to extract response - simple approach: get everything after the prompt
    $promptIndex = $content.IndexOf("What is the best template", [System.StringComparison]::OrdinalIgnoreCase)
    
    if ($promptIndex -ge 0) {
        # Skip past the prompt to find the response
        $afterPrompt = $content.Substring($promptIndex + 50)
        
        # Clean up the response - remove UI elements
        $cleanResponse = $afterPrompt -replace '(?i)(share|copy|regenerate|stop|new chat|settings|logout|menu)', ''
        $cleanResponse = $cleanResponse.Trim()
        
        if ($cleanResponse.Length -gt 100) {
            # Save the response
            $cleanResponse | Out-File -FilePath "deepseek_response.txt" -Encoding UTF8
            Write-Host "COMPLETE"
            exit 0
        }
    }
}

if ($stillGenerating) {
    Write-Host "GENERATING"
    exit 1
} else {
    Write-Host "WAITING"
    exit 2
}