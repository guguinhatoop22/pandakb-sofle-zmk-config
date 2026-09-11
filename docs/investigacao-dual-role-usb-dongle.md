# Investigação: alternância automática Dongle ⇄ USB direto (Dual Role) no PandaKB Sofle / ZMK v0.3

Status: **investigação concluída — nenhum código de firmware foi alterado neste PR.**
Base analisada: `zmkfirmware/zmk` tag `v0.3` (commit `edf5c08`, "chore(main): release 0.3.0"), Zephyr `3.5.0+zmk-fixes`.
Repositório alvo: `guguinhatoop22/pandakb-sofle-zmk-config` (`main`).

## Veredito (resposta ao critério final)

> **B. Existe uma solução experimental viável mantendo o core upstream intacto.**

Com uma ressalva honesta: ela é **B para o objetivo principal** (Left assume Central automaticamente quando o USB é conectado e volta a Peripheral quando o cabo sai, sem reflash, sem reset de settings, sem apagar bonds) e **C para dois sub-objetivos**:

- o cenário "dongle ligado **e** USB no Left ao mesmo tempo" é resolvido por **política de prioridade** (dongle vence, USB vira só alimentação), não por handover negociado com o dongle;
- em modo dongle, o OLED do Left passa a usar o layout de *central* (o Pokémon do layout peripheral só volta com trabalho extra), e o ZMK Studio precisa ficar inativo no Left enquanto ele é peripheral.

A afirmação do Antigravity — *"não é possível no ZMK v0.3 sem modificar o core"* — está **incorreta na conclusão**, embora quase todas as premissas dele estejam certas. O que ele não considerou é que o ZMK v0.3 **já ganhou** (via PR [#2886](https://github.com/zmkfirmware/zmk/pull/2886), merge em 2025-06-16) uma camada de *split transports* com `set_enabled(bool)`, `get_status().available` e callbacks de mudança de status — ou seja, **ligar e desligar o lado central e o lado peripheral em runtime já é API pública do core**. O que falta no upstream não é a capacidade de ligar/desligar; é apenas (a) compilar os dois papéis no mesmo binário e (b) um árbitro que decida qual papel está ativo. As duas coisas podem ser feitas **inteiramente fora do upstream**, a partir deste repositório.

Isso não é teoria: durante esta investigação um firmware híbrido foi **realmente compilado e linkado** (ver seção 5).

---

## 1. O que o diagnóstico do Antigravity acertou

Tudo isto foi confirmado lendo o código:

1. **`CONFIG_ZMK_SPLIT_ROLE_CENTRAL` é build-time e controla o CMake**, não só ifdefs:

   ```cmake
   # zmk/app/src/split/CMakeLists.txt:12-18
   if (CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
       target_sources(app PRIVATE central.c)
       zephyr_linker_sources(SECTIONS ../../include/linker/zmk-split-transport-central.ld)
   else()
       target_sources(app PRIVATE peripheral.c)
       zephyr_linker_sources(SECTIONS ../../include/linker/zmk-split-transport-peripheral.ld)
   endif()
   ```

2. **`central.c` e `peripheral.c` são mutuamente exclusivos** — tanto na camada genérica (`src/split/`) quanto na BLE (`src/split/bluetooth/CMakeLists.txt:4-10`, onde `service.c` + `peripheral.c` só entram quando **não** é central).

3. **Peripheral não recebe USB HID nem keymap**: o bloco `if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)` em `app/CMakeLists.txt:47-88` é o que traz `hid.c`, `endpoints.c`, `hid_listener.c`, `keymap.c`, `combo.c`, `ble.c`, `hog.c` e ~20 behaviors. E `CONFIG_ZMK_USB` tem `depends on (!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL)` (`app/Kconfig:121-126`).

4. **O peripheral usa Directed Advertising para o bond existente** — e pior do que ele descreveu:

   ```c
   // zmk/app/src/split/bluetooth/peripheral.c:51-73
   static void each_bond(const struct bt_bond_info *info, void *user_data) {
       ... bt_addr_le_copy(addr, &info->addr);   // sobrescreve: fica o ÚLTIMO bond
   }
   static int start_advertising(bool low_duty) {
       bt_foreach_bond(BT_ID_DEFAULT, each_bond, &central_addr);
       if (bt_addr_le_cmp(&central_addr, BT_ADDR_LE_NONE) != 0) {
           ... BT_LE_ADV_CONN_DIR(&central_addr) ...   // directed para 1 peer só
       } else {
           ... bt_le_adv_start(BT_LE_ADV_CONN, zmk_ble_ad, ...)  // undirected
       }
   }
   ```

   Com dois bonds, o Right anuncia dirigido para **um** deles (o último enumerado) e fica efetivamente invisível para o outro central.

5. **Troca real de papel exige central + peripheral no mesmo binário e um gerenciador de estado em runtime** — correto, e é exatamente a arquitetura recomendada aqui.

6. **Não existe solução upstream**: [issue #2885](https://github.com/zmkfirmware/zmk/issues/2885) ("Allow switching between central and peripheral mode at run time or boot time", aberta desde 2025-03, 18 👍) e [issue #3330](https://github.com/zmkfirmware/zmk/issues/3330) ("Dynamic Central Role Election & Handover") continuam abertas, sem implementação e sem PR.

## 2. O que está faltando no diagnóstico

Cinco pontos que mudam a conclusão:

### 2.1 `BT_MAX_PAIRED=1` **não** é o seu caso

O default de `BT_MAX_PAIRED`/`BT_MAX_CONN` para peripheral é 1 (`app/src/split/bluetooth/Kconfig:93-97`), mas é apenas *default*, e o **seu** `config/Sofle_dongle_right.conf` já sobrescreve:

```
CONFIG_BT_MAX_CONN=3
CONFIG_BT_MAX_PAIRED=3
```

Ou seja, **o Right já tem espaço para 3 bonds hoje**. O problema do Right nunca foi capacidade de bond — é **política de advertising** (item 4 acima). Isso derruba boa parte da complexidade estimada pelo Antigravity.

### 2.2 O ZMK v0.3 já tem seleção de transporte em runtime

`app/src/split/central.c` e `app/src/split/peripheral.c` (camada genérica, nova em 0.3) implementam:

```c
// zmk/app/src/split/central.c:166-197 (peripheral.c:70-100 é simétrico)
static int select_first_available_transport(void) {
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, t) {
        if (!t->api->get_status || t->api->get_status().available) {
            if (active_transport && active_transport->api->set_enabled)
                active_transport->api->set_enabled(false);
            active_transport = t;
            return active_transport->api->set_enabled(true);
        }
    }
    return -ENODEV;
}
```

E o transporte BLE implementa `set_enabled` de verdade dos dois lados:

- central (`src/split/bluetooth/central.c:310-333`): `true` → `start_scanning()`; `false` → `stop_scanning()` + `bt_conn_disconnect()` de todos os peripherals;
- peripheral (`src/split/bluetooth/peripheral.c:169-194`): `true` → `bt_le_adv_start(...)`; `false` → `bt_conn_disconnect()` + `bt_le_adv_stop()`.

**Essa é exatamente a operação "parar advertising Peripheral / iniciar BLE Central" que você descreveu na seção USB do pedido — ela já existe, testada, no core.** O precedente upstream é o próprio PR #2886, que usa um **pino GPIO de detecção** para tornar um transporte "available" em runtime; nós usaríamos o **estado do USB** no lugar do GPIO.

### 2.3 Só **um** símbolo colide entre os dois papéis

Compilando `src/split/central.c` e `src/split/peripheral.c` juntos, o único erro de link é:

```
multiple definition of `active_transport';
  app/src/split/central.c:24  vs  app/src/split/peripheral.c:25
```

Ambos são `const struct zmk_split_transport_*  *active_transport;` no escopo de arquivo, sem `static` (bug cosmético do upstream). Um `#define active_transport ...` antes de incluir o arquivo resolve, sem tocar no upstream. Todo o resto (`central_init`, `peripheral_init`, listeners, msgqs, work queues) já tem nomes distintos ou é `static`.

### 2.4 "Modificar o core" não é a única forma de mudar o comportamento do core

Este repositório **já faz isso hoje**, e o próprio autor escreveu o código: `boards/shields/nice_oled/CMakeLists.txt:105-115` usa

```cmake
zephyr_ld_options(
  -Wl,--wrap=zmk_rgb_underglow_calc_effect
  ...
  -Wl,--wrap=zmk_rgb_underglow_on
)
if(CONFIG_ZMK_SPLIT AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_ld_options(-Wl,--wrap=zmk_split_transport_peripheral_command_handler)
endif()
```

Ou seja, o efeito ripple já intercepta funções do core ZMK via `--wrap` a partir do shield local. A mesma técnica funciona para funções do host Bluetooth do Zephyr (comprovado na seção 5).

### 2.5 Módulo Zephyr pode compilar arquivos do próprio ZMK

Um módulo out-of-tree (`zephyr/module.yml` com `build: {cmake: ., kconfig: Kconfig}`) pode:

- fazer `target_sources(app PRIVATE ${APPLICATION_SOURCE_DIR}/src/split/peripheral.c ...)` — isto é, **re-adicionar** os arquivos que o CMake do upstream excluiu;
- emitir a seção de linker que faltou: `zephyr_linker_sources(SECTIONS ${APPLICATION_SOURCE_DIR}/include/linker/zmk-split-transport-peripheral.ld)`;
- **re-declarar símbolos Kconfig** que o upstream escondeu atrás de `if !ZMK_SPLIT_ROLE_CENTRAL` (Kconfig faz OR das dependências de definições múltiplas do mesmo símbolo).

Nada disso é fork do ZMK: o `west.yml` continua apontando para `zmk@v0.3`.

## 3. Limitações reais do ZMK v0.3 (as que sobram de verdade)

| # | Limitação | Impacto | Contornável no nosso repo? |
|---|---|---|---|
| L1 | Papel é build-time no CMake e no Kconfig | Precisa de módulo que re-adicione o lado ausente | Sim |
| L2 | `active_transport` duplicado (símbolo global) | Erro de link | Sim (`#define`) |
| L3 | `ble.c` só é compilado em builds central e **assume que toda conexão em role PERIPHERAL é um host HID** (`src/ble.c:501-530`, `:646-668`) | Em modo dongle, a conexão vinda do dongle seria contabilizada como "profile" e, se o profile ativo estiver ocupado, `auth_pairing_complete` chama `bt_unpair()` no dongle | Sim (`--wrap` em `bt_conn_auth_info_cb_register`; ou usar identidade BLE separada) |
| L4 | `update_advertising()` (`src/ble.c:178-221`) reinicia advertising HID sempre que o profile ativo não está conectado; **legacy advertising só admite 1 conjunto ativo** | Advertising HID e advertising split brigam pelo mesmo recurso | Sim (`--wrap bt_le_adv_start`, ou `BT_EXT_ADV` com 2 adv sets) |
| L5 | Peripheral faz directed adv para o **último** bond | Right fica preso a um único central | Sim (transporte peripheral próprio, prioridade 0) |
| L6 | `service.c` notifica com `bt_gatt_notify(NULL, ...)` (`:221`, `:267`, `:329`) → **todas** as conexões inscritas | Se dongle e Left conectarem ao Right ao mesmo tempo → teclas duplicadas | Sim (`CONFIG_BT_MAX_CONN=1` no Right = trava estrutural) |
| L7 | `keymap.c` e o listener do peripheral consomem o mesmo `zmk_position_state_changed` | No híbrido, uma tecla seria processada localmente **e** encaminhada ao dongle | Sim (listener do módulo roda antes de `keymap` e devolve `ZMK_EV_EVENT_HANDLED` — ordem verificada no ELF) |
| L8 | Telas do `nice_oled` são escolhidas em build-time por `CONFIG_ZMK_SPLIT_ROLE_CENTRAL` (`nice_oled/CMakeLists.txt:56-101`) | Left híbrido mostraria a tela de central também em modo dongle | Parcialmente (trocar widgets em runtime é trabalho extra) |
| L9 | Não há protocolo de handover entre centrais; o dongle não sabe que o Left "assumiu" | Cenário "dongle + USB" precisa de política, não de negociação | Mitigável (ver seção 11) |
| L10 | `reserve_peripheral_slot()` grava o endereço do peripheral em settings permanentemente (`src/ble.c:381-414`) | O primeiro dispositivo que anunciar o UUID split ocupa o slot 0 do Left | Aceitável (só o Right anuncia por perto) |
| L11 | ZMK Studio assume papel central | Studio no Left deve ficar inerte em modo dongle | Aceitável / documentável |

Note que **nenhuma** dessas limitações é "o BLE não permite" — são todas decisões de arquitetura do ZMK.

## 4. O que Zephyr/Bluetooth permitem mesmo que o ZMK não implemente

- **Dual role simultâneo**: `CONFIG_BT_CENTRAL=y` + `CONFIG_BT_PERIPHERAL=y` é configuração padrão e suportada; o build híbrido gerado nesta investigação tem os dois ligados. O nRF52840 pode manter conexões como central e como peripheral ao mesmo tempo (`BT_MAX_CONN`).
- **GATT server + GATT client coexistem** sem restrição (o híbrido tem o serviço split registrado *e* `BT_GATT_CLIENT=y`).
- **Start/stop de advertising e scan em runtime**: `bt_le_adv_start`/`bt_le_adv_stop`, `bt_le_scan_start`/`bt_le_scan_stop`, `bt_conn_disconnect` — tudo já usado pelo ZMK.
- **Directed advertising com timeout**: `BT_LE_ADV_CONN_DIR` (alta duty, timeout de 1,28 s → `connected(err = BT_HCI_ERR_ADV_TIMEOUT)`) e `BT_LE_ADV_CONN_DIR_LOW_DUTY`. O upstream já trata esse timeout (`peripheral.c:93-96`) — dá para **alternar o alvo** a cada timeout, que é exatamente a estratégia híbrida "directed → timeout → outro alvo/open" que você pediu.
- **Múltiplos bonds**: limitados só por `CONFIG_BT_MAX_PAIRED` (hoje 3 no seu Right).
- **Múltiplas identidades**: `CONFIG_BT_ID_MAX>1` + `bt_id_create()` dá endereços/bond stores separados; `bt_le_adv_param.id` escolhe a identidade do advertising. Permitiria manter o bond do dongle numa identidade e os profiles de host noutra, isolando o problema L3.
- **Advertising sets múltiplos**: `CONFIG_BT_EXT_ADV` + `BT_EXT_ADV_MAX_ADV_SET=2` + `bt_le_ext_adv_create()` permitiria anunciar HID e split ao mesmo tempo. Não é necessário na arquitetura recomendada (os dois nunca precisam estar ativos juntos), mas é a saída caso se queira "peripheral do dongle **e** HID BLE" simultâneos.
- **Filter accept list** (`bt_le_filter_accept_list_add`) para restringir quem pode conectar.
- **Interceptação de símbolos**: `-Wl,--wrap=` do GNU ld funciona para chamadas cross-TU, inclusive para o host BLE do Zephyr (verificado no ELF).

## 5. É possível criar um firmware Dual Role? — **Sim, e foi compilado**

Prova experimental, feita fora do repositório (`/tmp`, nada commitado):

- Workspace real: `west init -l zmk/app` no `zmk@v0.3`, Zephyr SDK 0.16.3, `nice_nano_v2`.
- **Baseline** `Sofle_L nice_oled` com o seu `config/` e o seu `boards/`:
  `FLASH 339.928 B (41,91 % de 792 KB) · RAM 127.388 B (48,59 % de 256 KB)`.
- **Híbrido**: mesmo alvo + um módulo out-of-tree que adiciona `src/split/peripheral.c` (com `active_transport` renomeado), `src/split/bluetooth/peripheral.c`, `src/split/bluetooth/service.c`, a seção de linker do registro de transportes peripheral, e um `dual_role.c` de árbitro:
  `FLASH 343.556 B (42,36 %) · RAM 129.300 B (49,32 %)` → **+3,6 KB de flash, +1,9 KB de RAM**. Zero alterações no upstream.

Evidências extraídas do ELF resultante:

```
# os dois papéis coexistem
0002dfec t central_init          0002a03c t peripheral_init
20008864 B active_transport      200065b0 B zmk_peripheral_active_transport

# os dois registros de transporte existem, cada um na sua seção
00079df4 D bt_central            _zmk_split_transport_central_list_{start,end}
00079df8 D dual_role_peripheral  <-- transporte local (prioridade 0)
00079dfc D bt_peripheral         <-- transporte do upstream (prioridade 1)

# ordem das inscrições em zmk_position_state_changed (endereços crescentes)
0x77e88 split_peripheral  <  0x77e98 dual_role  <  0x77ea8 activity
        <  0x77ec0 behavior_hold_tap  <  0x77ef0 keymap  <  0x77f90 ripple_rgb

# --wrap redireciona chamadas do próprio ble.c do ZMK
update_advertising: bl 2a670 <__wrap_bt_le_adv_start>
ble.c/peripheral.c: bl 2a6a0 <__wrap_bt_conn_auth_info_cb_register>
```

Três coisas importantes ficam provadas aí:

1. o binário híbrido **linka e cabe** com folga (57 % de flash livre);
2. um transporte declarado no **nosso** repositório com prioridade 0 fica **antes** do transporte BLE do upstream na lista — ou seja, dá para substituir a política de advertising do Right sem tocar no core;
3. o listener do **nosso** módulo é despachado **antes** do `keymap` (módulos entram no target `app` antes das fontes do próprio app), então devolver `ZMK_EV_EVENT_HANDLED` em modo peripheral bloqueia o keymap local — resolvendo L7. E o listener `split_peripheral` roda antes de nós, então o encaminhamento ao dongle acontece **antes** do bloqueio. A ordem é exatamente a necessária.

## 6. Quais arquivos do ZMK realmente precisariam ser alterados

**Estritamente necessários: nenhum.** Foi por isso que o build acima funcionou.

Se um dia quisermos mandar isso upstream (ou se quisermos uma solução mais limpa), a lista mínima seria:

| Arquivo | Alteração | Por quê |
|---|---|---|
| `app/src/split/{central,peripheral}.c` | tornar `active_transport` `static` | remove a única colisão de símbolo |
| `app/src/split/CMakeLists.txt` + `Kconfig` | novo `ZMK_SPLIT_ROLE_DUAL` que compila os dois lados | remove o `#define`/shim |
| `app/CMakeLists.txt` | `if (... OR CONFIG_ZMK_SPLIT_ROLE_DUAL)` no bloco de keymap/HID | idem |
| `app/src/split/bluetooth/peripheral.c` | advertising com rotação entre bonds | remove a necessidade do transporte custom |
| `app/src/ble.c` | API para suspender advertising de host e ignorar conexões split | remove os dois `--wrap` |
| `app/src/keymap.c` | API `zmk_keymap_set_enabled()` | remove o truque de ordem de listener |

Isso é, aliás, um bom esboço de PR para a issue #2885.

## 7. O que dá para fazer só no nosso shield/repositório

Tudo o que a solução precisa:

```
boards/shields/Sofle_hybrid/            # novo shield (Left híbrido) + reuso do Sofle.dtsi
  Kconfig.shield / Kconfig.defconfig    # ROLE_CENTRAL=y, PERIPHERALS=1, defaults de peripheral
  Sofle_hybrid_left.overlay             # = Sofle_L.overlay (kscan real, encoder, VDDH)

boards/shields/nice_oled/               # (opcional) tela por modo
zephyr/module.yml                       # + build: {cmake: ., kconfig: Kconfig}
CMakeLists.txt (raiz)                   # re-adiciona os fontes do papel peripheral + linker section
Kconfig (raiz)                          # re-declara ZMK_SPLIT_BLE_PERIPHERAL_* fora do `if !CENTRAL`
src/dual_role/
  dual_role.c                           # máquina de estados + política
  dual_role_wraps.c                     # __wrap_bt_le_adv_start / __wrap_bt_conn_auth_info_cb_register
  peripheral_role_shim.c                # #define active_transport ... + #include do peripheral.c
  right_adv_transport.c                 # transporte peripheral do Right (rotação de directed adv)
config/Sofle_hybrid_left.conf           # BT_MAX_PAIRED/CONN, ZMK_USB, etc.
config/Sofle_hybrid_right.conf          # BT_MAX_CONN=1 (trava anti-duplo-central)
build.yaml                              # 2 alvos novos
```

Nenhuma alteração em `config/west.yml` (continua `zmk@v0.3`), nenhum fork, nenhum patch aplicado ao core.

## 8. Como detectar USB/VBUS em runtime

O ZMK já entrega isso pronto (`app/src/usb.c`), e o evento é global:

```c
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);   // NONE | POWERED | HID
bool zmk_usb_is_hid_ready(void);                        // configurado de fato
ZMK_SUBSCRIPTION(dual_role, zmk_usb_conn_state_changed);
```

Mapeamento em `usb.c:34-51`:

- `USB_DC_DISCONNECTED` / `USB_DC_UNKNOWN` → `ZMK_USB_CONN_NONE` (nada ligado);
- `USB_DC_CONNECTED` / `USB_DC_RESET` etc. → `ZMK_USB_CONN_POWERED` (**VBUS presente, mas sem enumeração — carregador/power bank**);
- `USB_DC_CONFIGURED`/`SUSPEND`/`RESUME`/`SOF` → `ZMK_USB_CONN_HID` (**enumerado num host de verdade**).

Isso é decisivo para a política: **só promovemos a central quando o estado for `ZMK_USB_CONN_HID`**, nunca em `POWERED`. Um power bank não deve derrubar o modo dongle.

Detalhes relevantes:

- No nRF52840, o VBUS é detectado pelo bloco POWER e o driver `usb_dc_nrfx` gera os status acima; não é preciso GPIO extra nem `nrfx_power_usbstatus_get()` direto.
- `CONFIG_ZMK_USB` exige papel central — satisfeito, porque o híbrido é *compilado* como central e só se comporta como peripheral.
- `usb_enable()`/`usb_disable()` existem se quisermos desligar a pilha USB em modo dongle (economia marginal; provavelmente não vale).
- O `endpoints.c` já escolhe USB automaticamente quando o USB fica pronto (`preferred_transport` default USB) — então a parte "USB HID → PC" não precisa de código nenhum.

## 9. Como iniciar/parar Central e Peripheral em runtime

Ordem de operações do árbitro (todas as chamadas já existem):

**Promoção (peripheral → central), disparada por `ZMK_USB_CONN_HID` + dongle ausente:**

1. `bt_peripheral.api->set_enabled(false)` → desconecta o central atual (se houver) e `bt_le_adv_stop()`;
2. marcar modo = CENTRAL → o `__wrap_bt_le_adv_start` passa a **liberar** o advertising de host do `ble.c` e a **bloquear** o split;
3. apontar `zmk_peripheral_active_transport` para um transporte no-op (silencia o encaminhamento de teclas ao dongle sem log spam);
4. `bt_central.api->set_enabled(true)` → `start_scanning()` → conecta ao Right, descobre GATT, subscreve posições/bateria;
5. nosso listener passa a devolver `ZMK_EV_EVENT_BUBBLE` → o keymap local volta a processar;
6. `endpoints.c` já selecionou USB sozinho.

**Rebaixamento (central → peripheral), disparado por USB sair de HID:**

1. `bt_central.api->set_enabled(false)` → `stop_scanning()` + `bt_conn_disconnect()` do Right;
2. modo = PERIPHERAL → wrap volta a bloquear advertising de host e liberar o split;
3. restaurar `zmk_peripheral_active_transport` para o transporte BLE real;
4. `bt_peripheral.api->set_enabled(true)` → volta a anunciar o serviço split; dongle reconecta; Right (livre) também volta ao dongle;
5. listener volta a devolver `ZMK_EV_EVENT_HANDLED` → keymap local desligado.

**Plano B de baixo risco:** em vez da troca a quente, gravar o modo desejado em settings e chamar `sys_reboot()`. O nice!nano reinicia em ~1 s, a imagem é a mesma (**não é reflash**), e todo o estado BLE/HID/keymap nasce limpo no papel certo — elimina de uma vez qualquer risco de estado meio-trocado. A troca a quente é mais elegante; o reboot é mais seguro. Recomendo implementar a quente **com fallback automático para reboot** se a transição não convergir em N segundos.

## 10. Como resolver os bonds do Right

O Right **já pode** guardar dongle + Left (é `CONFIG_BT_MAX_PAIRED=3` hoje). O que muda:

1. **Substituir a política de advertising** com um transporte peripheral próprio registrado com prioridade 0 (comprovadamente vem antes do `bt_peripheral` do upstream na lista, então o upstream nunca é habilitado e fica inerte). Ele reaproveita o `service.c` do core (`zmk_split_transport_peripheral_bt_report_event`) — só a máquina de advertising é nossa. Estratégia recomendada (a "híbrida" que você descreveu):

   ```
   estado A: BT_LE_ADV_CONN_DIR(dongle)   -> 1,28 s, timeout => estado B
   estado B: BT_LE_ADV_CONN_DIR(left)     -> 1,28 s, timeout => estado C
   estado C: BT_LE_ADV_CONN (undirected, UUID split) -> 5 s  => estado A
   ```

   Preferência natural pelo dongle (ele é tentado primeiro), reconexão rápida no caso comum, e o Left ainda consegue capturar o Right em ~1,3 s quando o dongle não está presente. O estado C mantém o pareamento inicial funcionando.
   Alternativa mais simples: **só undirected** com o UUID split; funciona, custa um pouco mais de bateria e reconexão levemente mais lenta.

2. **`CONFIG_BT_MAX_CONN=1` no Right** (hoje 3). Isso é uma trava de hardware contra L6: com um único slot de conexão, é fisicamente impossível dongle e Left receberem as mesmas notificações ao mesmo tempo. Custo: nenhum (o Right nunca precisa de mais de uma conexão).

3. **Nada de `bt_unpair`.** Os dois bonds convivem; o Right nunca apaga nada.

Cuidado a documentar: `CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y` está ativo e o Zephyr pode despejar o bond mais antigo se os 3 slots encherem. Com 2 bonds previstos (dongle + Left) e 3 slots, há margem.

## 11. Como evitar conflitos com o dongle

Política recomendada — **"o dongle sempre ganha; o USB no Left só assume se o dongle não estiver presente"**:

| Situação | Comportamento |
|---|---|
| Boot | Left nasce **peripheral** (= comportamento atual, é o estado seguro) |
| Dongle ligado, sem USB | Dongle conecta ao Left em ~1-2 s → Left fica peripheral para sempre |
| Dongle ligado **+ USB no Left** | Left **continua peripheral**; USB é só alimentação/carga. Sem split-brain, sem tecla duplicada |
| Dongle ausente + USB no Left | Sem conexão de central por `T` s (recomendo 5-8 s) **e** `ZMK_USB_CONN_HID` → Left promove-se a central, captura o Right |
| USB removido do Left (central) | Rebaixa; Right desconecta e volta a anunciar; se o dongle estiver ligado, ele recupera os dois lados |
| Dongle religado enquanto o Left é central | O dongle não acha ninguém (Left não anuncia; Right está conectado ao Left). Nada quebra. Basta tirar o USB para voltar ao modo dongle |

Por que isto é seguro e não precisa de handover negociado: a condição de promoção usa um fato que o Left **observa diretamente** — "existe um central conectado a mim?" (o `bt_peripheral` já expõe isso via `zmk_split_bt_peripheral_is_connected()` e o evento `zmk_split_peripheral_status_changed`). Se o dongle está vivo, ele conecta; se conectou, não promovemos. As duas travas adicionais (`BT_MAX_CONN=1` no Right; Left nunca anuncia split enquanto central) tornam o estado "dongle → Right **e** Left → Right ao mesmo tempo" inalcançável.

Opcional, se você quiser forçar o modo USB mesmo com o dongle plugado: um behavior/combo no keymap (`&dual_role_force_central`) que ignora a checagem de dongle. Explícito, do usuário, sem heurística.

## 12. Como preservar os bonds

- **Nunca** chamar `bt_unpair()`, nunca `settings_reset`, nunca `CONFIG_ZMK_BLE_CLEAR_BONDS_ON_START`.
- Left híbrido guarda: bond do dongle (feito como peripheral) + bond do Right (feito como central, endereço também persistido em `ble/peripheral_addresses/0`) + profiles de host. Recomendo `CONFIG_BT_MAX_PAIRED=5` no Left (com `ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1`, sobram 4 profiles de host) e `CONFIG_BT_MAX_CONN>=3`.
- **O ponto delicado (L3)**: quando o dongle parear com o Left híbrido, o `ble.c` verá uma conexão em role PERIPHERAL e, em `auth_pairing_complete` (`src/ble.c:646-668`), ou grava o dongle como profile de host, ou — se o profile ativo estiver ocupado — executa `bt_unpair(BT_ID_DEFAULT, dst)` no dongle. Duas mitigações, ambas locais:
  1. `--wrap bt_conn_auth_info_cb_register` para capturar o callback do `ble.c` e mantê-lo desregistrado enquanto estivermos em modo peripheral (o wrap já foi comprovado funcionando nas 3 chamadas do binário);
  2. ou `CONFIG_BT_ID_MAX=2` + `bt_id_create()`, colocando o link com o dongle numa identidade separada — o `ble.c` só mexe em `BT_ID_DEFAULT`, então o bond do dongle fica fora do alcance dele. Mais limpo conceitualmente, mais trabalho (o transporte peripheral precisa anunciar com `adv_param.id = 1`).
- Bonds já existentes continuam válidos: os endereços BLE não mudam ao trocar de firmware (mesma identidade, mesma partição de settings), então **o dongle não precisa reparear** quando o Left híbrido for gravado. O Right precisará parear **uma vez** com o Left (evento novo), e isso não afeta o bond dele com o dongle.

## 13. Complexidade estimada

Global: **média-alta**. Decomposta (a parte que assustava é a barata):

| Componente | Complexidade | Observação |
|---|---|---|
| Compilar os dois papéis no mesmo binário | **baixa** | ~40 linhas de CMake/Kconfig + shim; **já validado** |
| Habilitar/desabilitar transportes em runtime | **baixa** | API pronta no core |
| Detecção de USB | **baixa** | evento pronto no core |
| Bloquear keymap local em modo peripheral | **baixa** | 1 listener; ordem já verificada no ELF |
| Supressão do advertising HID do `ble.c` | **média** | `--wrap`, técnica já usada neste repo |
| Advertising rotativo do Right | **média** | ~150 linhas, um transporte novo |
| Máquina de estados + política antichoque | **média** | timers, histerese, casos de borda |
| Isolar o `ble.c` do bond do dongle (L3) | **média-alta** | wrap ou 2ª identidade |
| Tela do OLED por modo (L8) | **alta** | widgets são escolhidos em build-time |
| Handover negociado com o dongle (não recomendado) | **extremamente alta** | exigiria protocolo novo entre centrais |

Risco maior não é o código: é **validação em hardware** (transições repetidas, sleep/soft-off durante a troca, reconexão do Right, consumo em modo peripheral com um binário de central).

## 14. Solução recomendada

**Arquitetura "Left de personalidade dupla", implementada como módulo Zephyr local, com árbitro por estado do USB e política dongle-first.**

```
                      imagem única no Left (compilada como ROLE_CENTRAL)
   ┌──────────────────────────────────────────────────────────────────────┐
   │  keymap · HID · endpoints · ble.c/hog · USB     (do build central)   │
   │  split/central.c + bluetooth/central.c          (transporte central) │
   │  split/peripheral.c + bluetooth/{peripheral,service}.c  (readicionado│
   │                                                  pelo nosso módulo)  │
   │  dual_role.c  ── árbitro ─────────────────────────────────────────── │
   └──────────────────────────────────────────────────────────────────────┘
                    │                                   │
     modo PERIPHERAL│ (default, seguro)                 │modo CENTRAL
                    ▼                                   ▼
   split adv ligado, host adv bloqueado      scan ligado, split adv bloqueado
   keymap local BLOQUEADO                    keymap local ATIVO
   teclas → dongle                           Right → Left → USB HID → PC

   gatilho: ZMK_USB_CONN_HID  E  sem central conectado por T s  → promove
   gatilho: USB sai de HID                                       → rebaixa
```

- **Dongle**: firmware **inalterado**.
- **Right**: mesma imagem de hoje + transporte de advertising próprio + `BT_MAX_CONN=1`. Um flash único.
- **Left**: imagem híbrida nova. Um flash único.
- **Depois disso, nunca mais reflash**: a troca é 100 % em runtime.
- Artefatos novos no `build.yaml`: `Sofle_hybrid_L` e `Sofle_hybrid_R` (mantendo os atuais intactos, para poder voltar atrás).

Descartadas, com motivo:

- **Duas imagens + bootloader escolhendo por VBUS**: o nice!nano usa o bootloader UF2 da Adafruit (slot único, sem MCUboot dual-slot). Inviável sem trocar bootloader.
- **Dongle virar ponte BLE-HID→USB** (Left sempre central, dongle como "host" BLE): tecnicamente elegante e mataria o problema de papel de vez — mas o Zephyr upstream não tem cliente HOGP (só o nRF Connect SDK tem), exigiria escrever o cliente HID GATT do zero **e** o dongle perderia bateria dos peripherals, layer e ZMK Studio na tela do Prospector. Custo/benefício pior. Fica registrado como Plano C.
- **Apagar bonds ao conectar USB**: rejeitado por requisito (e desnecessário).

## 15. Plano de implementação

**Fase 0 — Preparação (sem risco)**
1. Criar branch de trabalho; **não** mexer nos alvos atuais do `build.yaml`.
2. Promover o repo a módulo com código: `zephyr/module.yml` ganha `build: {cmake: ., kconfig: Kconfig}` (mantendo `board_root: .`).
3. Confirmar que os 8 artefatos atuais continuam compilando no CI.

**Fase 1 — Binário híbrido (o passo já validado)**
4. `CMakeLists.txt` raiz: re-adicionar `src/split/peripheral.c` (via shim com `#define active_transport`), `src/split/bluetooth/peripheral.c`, `src/split/bluetooth/service.c` e `zephyr_linker_sources(... zmk-split-transport-peripheral.ld)`, tudo sob um `CONFIG_SOFLE_DUAL_ROLE`.
5. `Kconfig` raiz: re-declarar `ZMK_SPLIT_BLE_PERIPHERAL_{STACK_SIZE,PRIORITY,POSITION_QUEUE_SIZE}`.
6. Novo shield `Sofle_hybrid_left` (kscan/encoder/RGB/OLED iguais ao `Sofle_L`, `ZMK_SPLIT_ROLE_CENTRAL=y`, `ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1`) + `config/Sofle_hybrid_left.conf` (`BT_MAX_PAIRED=5`, `BT_MAX_CONN=4`).
7. Critério de saída: compila e linka; medir flash/RAM (esperado ≈ +4 KB / +2 KB sobre o `Sofle_L` atual).

**Fase 2 — Árbitro**
8. `src/dual_role/dual_role.c`: enum de modo, listener de `zmk_usb_conn_state_changed` e `zmk_split_peripheral_status_changed`, timer de promoção (`T = 6 s`), funções `promote()`/`demote()` conforme a seção 9.
9. Listener de `zmk_position_state_changed` devolvendo `HANDLED` em modo peripheral; **verificação em build** de que o índice da nossa inscrição é menor que o de `zmk_listener_keymap` (dá para checar em runtime no init e logar erro se a ordem mudar num upgrade do ZMK).
10. Transporte peripheral no-op para desligar o encaminhamento em modo central.

**Fase 3 — Arbitragem de advertising**
11. `zephyr_ld_options(-Wl,--wrap=bt_le_adv_start)` + `__wrap_bt_le_adv_start` com flag "esse start é nosso".
12. `--wrap bt_conn_auth_info_cb_register` para poder soltar o bookkeeping de profile do `ble.c` em modo peripheral (mitigação de L3). Reavaliar aqui se vale trocar por `BT_ID_MAX=2`.
13. Teste de bancada: com o dongle ligado, confirmar que o Left **não** aparece como teclado BLE pareável.

**Fase 4 — Right**
14. `right_adv_transport.c`: transporte peripheral prioridade 0 com a máquina directed(dongle) → directed(left) → undirected, reaproveitando `zmk_split_transport_peripheral_bt_report_event`.
15. `config/Sofle_hybrid_right.conf`: `CONFIG_BT_MAX_CONN=1`, `BT_MAX_PAIRED=3` (mantido).
16. Teste: Right reconecta ao dongle em <2 s no caso comum; e é capturado pelo Left em <3 s quando o dongle está desligado.

**Fase 5 — Validação em hardware (a parte cara)**
17. Matriz de testes: 20 ciclos plugar/desplugar; plugar com dongle ligado (deve continuar peripheral); dongle desligado no meio do uso; sleep/soft-off durante cada modo; `&bt` e `&rgb_ug` em cada modo; bateria em 24 h no modo dongle; latência percebida nos dois modos.
18. Instrumentar com logs (`CONFIG_ZMK_USB_LOGGING`) e, se necessário, mostrar o modo no OLED.
19. Fallback automático: se `promote()`/`demote()` não convergirem em 10 s, `sys_reboot()` com o modo persistido em settings.

**Fase 6 — Acabamento (opcional)**
20. Tela do OLED por modo (L8), indicador de modo, behavior de força manual, e — se tudo estiver estável — abrir PR upstream contra a issue #2885 com a versão limpa (seção 6).

---

### Apêndice: referências de código verificadas

| Assunto | Local (zmk v0.3) |
|---|---|
| Gating de papel no CMake | `app/src/split/CMakeLists.txt:12-18`, `app/src/split/bluetooth/CMakeLists.txt:4-10`, `app/CMakeLists.txt:47-99` |
| Seleção de transporte em runtime | `app/src/split/central.c:166-227`, `app/src/split/peripheral.c:70-131` |
| API de transporte | `app/include/zmk/split/transport/{central,peripheral,types}.h` |
| `set_enabled` central | `app/src/split/bluetooth/central.c:310-333` |
| `set_enabled` peripheral | `app/src/split/bluetooth/peripheral.c:169-194` |
| Directed adv do peripheral | `app/src/split/bluetooth/peripheral.c:51-73` |
| Notificação para todos os conectados | `app/src/split/bluetooth/service.c:221,267,329` |
| Advertising de host / profiles | `app/src/ble.c:146-221`, `:501-556`, `:626-668` |
| Persistência do endereço do peripheral | `app/src/ble.c:381-414` |
| Defaults de `BT_MAX_PAIRED`/`BT_MAX_CONN` | `app/src/split/bluetooth/Kconfig:93-97`, `Kconfig.defaults:11-23` |
| Estado do USB | `app/src/usb.c:32-76`, `app/src/endpoints.c:280-295` |
| Dependência de papel do `ZMK_USB` | `app/Kconfig:121-126` |
| Precedente de detecção em runtime | PR [#2886](https://github.com/zmkfirmware/zmk/pull/2886), `app/src/split/wired/central.c:363-432` |
| Pedidos upstream em aberto | issues [#2885](https://github.com/zmkfirmware/zmk/issues/2885), [#3330](https://github.com/zmkfirmware/zmk/issues/3330) |
| `--wrap` já usado neste repo | `boards/shields/nice_oled/CMakeLists.txt:105-115` |
