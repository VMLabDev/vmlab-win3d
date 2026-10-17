<#
.SYNOPSIS
Parses every script in this directory with the running PowerShell (run it
under Windows PowerShell 5.1, the version the guest scripts target) and
reports syntax errors. Exit 0 when all parse.
#>
$failed = 0
foreach ($f in Get-ChildItem -Path $PSScriptRoot -Filter '*.ps1') {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
    if ($errors) {
        $failed++
        foreach ($e in $errors) { Write-Output ("{0}:{1}: {2}" -f $f.Name, $e.Extent.StartLineNumber, $e.Message) }
    } else {
        Write-Output "ok   $($f.Name)"
    }
}
Write-Output "PowerShell $($PSVersionTable.PSVersion)"
exit $failed
