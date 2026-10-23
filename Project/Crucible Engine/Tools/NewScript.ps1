param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z_][A-Za-z0-9_]*$')][string]$Name,
    [ValidatePattern('^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$')][string]$Namespace = 'Crucible.Scripts',
    [switch]$Entity
)
$ErrorActionPreference = 'Stop'
if (@('OnCreate','OnUpdate','OnDestroy') -ccontains $Name) {
    throw 'Choose a class name different from its OnCreate, OnUpdate and OnDestroy callback methods.'
}
foreach ($reserved in @('Crucible.EntityScript','Crucible.EntityIdentity','Crucible.Log','Crucible.Animator')) {
    if ((($Namespace+'.'+$Name) -ceq $reserved) -or ($Namespace -ceq $reserved) -or $Namespace.StartsWith($reserved+'.',[StringComparison]::Ordinal)) {
        throw 'That name is reserved by ScriptCore. Use a namespace such as Crucible.Scripts.'
    }
}
$directory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../ManagedScripts/Scripts'))
$path = Join-Path $directory ($Name + '.cs')
if (Test-Path -LiteralPath $path) { throw "Script already exists: $path" }
$escapedNamespace = (($Namespace.Split('.') | ForEach-Object { '@' + $_ }) -join '.')
$source = @"
namespace $escapedNamespace
{
    public sealed class @$Name
    {
        public void OnCreate()
        {
        }

        public void OnUpdate(float deltaTime)
        {
        }

        public void OnDestroy()
        {
        }
    }
}
"@
if ($Entity) {
    $template = Join-Path $PSScriptRoot '../ManagedScripts/Templates/EntityScript.cs.txt'
    $source = (Get-Content -LiteralPath $template -Raw).Replace('{{NAMESPACE}}',$escapedNamespace).Replace('{{CLASS}}',('@'+$Name)).Replace('{{NAME}}',$Name)
}
New-Item -ItemType Directory -Path $directory -Force | Out-Null
New-Item -ItemType File -Path $path -Value $source | Out-Null
Write-Output "Created $path"
if ($Entity) {
    Write-Output "Close the app, build scripts and restart. Then search $Name in the entity's Add Component menu and attach the C# class."
    return
}
Write-Output 'Build with Tools/BuildScripts.ps1. Opt in through scripting.instances:'
@{assembly='Managed/Crucible.Scripts.dll';namespace=$Namespace;class=$Name;enabled=$true} | ConvertTo-Json
Write-Output 'These callbacks are standalone. See Docs/ScriptingDevelopment.md for lifetime and restart rules.'
