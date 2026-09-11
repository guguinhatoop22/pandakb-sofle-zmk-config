# Publicar em https://github.com/guguinhatoop22/zmk-ripple-rgb

**Só com a conta guguinhatoop22** (Antigravity / seu PC). O Cursor agent NÃO deve dar push neste repo.

Os arquivos já estão no GitHub do Sofle (não só no cloud do Cursor):

- Branch: `cursor/ripple-west-module-7054`
- Pasta: `modules/zmk-ripple-rgb/`
- Commit: `e31eef3`

## PowerShell (Windows)

```powershell
# 1) Pegar o módulo do Sofle (só leitura desta branch)
cd $env:USERPROFILE\Documents   # ou onde preferir
git clone --branch cursor/ripple-west-module-7054 --depth 1 `
  https://github.com/guguinhatoop22/pandakb-sofle-zmk-config.git sofle-ripple-src

# 2) Clonar o repo vazio do módulo
git clone https://github.com/guguinhatoop22/zmk-ripple-rgb.git
cd zmk-ripple-rgb

# 3) Copiar conteúdo (exceto PUBLISH.md) para a raiz
Copy-Item -Recurse -Force ..\sofle-ripple-src\modules\zmk-ripple-rgb\* .
Remove-Item -Force .\PUBLISH.md -ErrorAction SilentlyContinue

# 4) Commit e push com a sua autoria
git add -A
git -c user.name=guguinhatoop22 -c user.email=guguinhatoop22@users.noreply.github.com `
  commit -m "feat: initial ZMK west module for Sofle reactive Ripple RGB"
git push -u origin main
```

Depois disso, no Sofle: adicionar o módulo em `config/west.yml` e remover o ripple in-tree do `zmk-nice-oled` (passo separado).
