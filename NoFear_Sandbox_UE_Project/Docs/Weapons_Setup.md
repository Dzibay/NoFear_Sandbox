# Оружие: настройка в редакторе

Код оружия на C++:

- `UNoFearWeaponData` — Data Asset со всеми параметрами ствола.
- `ANoFearWeapon` — актор оружия. Его создаёт компонент, руками на уровень ставить не нужно.
- `UNoFearWeaponComponent` — уже стоит на `ANoFearCharacter`, а значит и на `CBP_SandboxCharacter`. Компонент выдаёт оружие, переключает его, принимает ввод, ставит позу GASP (`OverlayState`) и каждый кадр передаёт отдачу рук в анимблюпринт.

Почему больше не залипает: огонь привязан к **Started / Completed / Canceled**, а не к Triggered. Очередь стреляет по одному таймеру, и отпускание кнопки всегда его гасит. Плюс спуск отпускается при смене контроллера и при смене оружия.

## Что вернулось из старой версии

Старая система (коммит `d4bcd02`) состояла из `BP_Weapon*`, `CBP_SandboxCharacter` и `ABP_SandboxCharacter`. Вот что из неё сохранилось и как работает теперь:

| Что | Где было | Как теперь |
|---|---|---|
| Позы держания (винтовка, пистолет в одной руке) | Оверлеи GASP `OverlaySystem/Overlays/*`, выбор через `OverlayState` | То же самое. Значение ставит компонент из поля **Overlay State** в Data Asset'е. |
| Перезарядка | `AM_Character*_Reload_Montage` в слоте `UpperBody` | То же самое: поле **Character Reload Montage**. |
| Затвор и магазин самого ствола | `AM_Rifle_Fire_Montage` и др. на меше оружия | Поля **Weapon Fire / Reload Animation**: ставятся `A_*_Fire` / `A_*_Reload`. |
| Рывок рук при выстреле | `ABP_SandboxCharacter`: узлы Modify Bone на upperarm_r / lowerarm_r / hand_r и spine_01–03, сила `Recoil_Amount` | Те же узлы. Повороты костей берутся из Data Asset'а (**Recoil → Arms**), силу считает C++ по той же кривой: пик через 0.05 с, покой к 0.5 с. |
| Оружие на спине и поясе | `SocketPassive` у каждого `BP_Weapon_*` | Поле **Holster Socket**. Убранное оружие теперь висит на теле, а не пропадает. |

`ABP_SandboxCharacter` уже возвращён из `d4bcd02` вместе со слотом `UpperBody` и узлами отдачи. Ему нужна маленькая починка, см. шаг 5.

---

## 0. Пересобрать редактор

Если C++ менялся при открытом редакторе, **закрой** редактор и собери Editor-таргет. Потом открой проект снова через `C:\NoFear_Sandbox\...\NoFear_Sandbox_UE_Project.uproject`.

## 1. Ввод (папка `Content/Input`)

Создай Input Action (ПКМ → Input → Input Action):

| Ассет | Value Type |
|---|---|
| `IA_Fire` | Digital (bool) |
| `IA_Reload` | Digital (bool) |
| `IA_SwitchWeapon` | Axis1D (float) |
| `IA_Holster` | Digital (bool) |

`IA_Aim` уже есть и привязан к ПКМ в `IMC_Sandbox`, его используем как есть.

Создай Input Mapping Context `IMC_Weapon` с такими привязками:

- `IA_Fire` → **Left Mouse Button**, плюс Gamepad Right Trigger.
- `IA_Reload` → **R**.
- `IA_SwitchWeapon` → **Mouse Wheel Axis**.
- `IA_Holster` → **X**.

**Triggers не добавляй**, оставь пустыми. Для огня это важно: никаких Hold и Pulse.

## 2. Data Asset'ы оружия (папка `Content/Weapons/Data`)

ПКМ → Miscellaneous → Data Asset → **NoFearWeaponData**. Создай `DA_Rifle`, `DA_Pistol` и `DA_Shotgun`.

### Вид, сокеты, анимации

Это значения из старой версии.

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Overlay State | `Rifle` | `Pistol1H` | `Rifle` |
| Mesh | `SK_Rifle` | `SK_Pistol` | `SK_Shotgun` |
| Attach Socket | `overlay_rifle` | `overlay_pistol` | `overlay_rifle` |
| Holster Socket | `spine_weapon_first_socket` | `pelvis_weapon_first_socket` | `spine_weapon_second_socket` |
| Character Fire Montage | — | — | — |
| Character Reload Montage | `AM_CharacterRifle_Reload_Montage` | `AM_CharacterPistol_Reload_Montage` | `AM_CharacterShotGun_Reload_Montage` |
| Character Equip Montage | — | `AM_CharacterPistol_Equip_Montage` | — |
| Weapon Fire Animation | `A_Rifle_Fire` | `A_Pistol_Fire` | `A_Shotgun_Fire` |
| Weapon Reload Animation | `A_Rifle_Reload` | `A_Pistol_Reload` | `A_Shotgun_Reload` |

- **Mesh**: возьми скелетный меш из `Content/Weapons/<тип>/Meshes`. Анимации `A_*_Fire` и `A_*_Reload` сделаны под его скелет.
- **Character Fire Montage** в старой версии не использовался: выстрел показывала процедурная отдача рук (ниже). Если захочется сильнее, можно поставить `AM_CharacterRifle_Fire_Montage` или `AM_CharacterPistol_Fire_Montage`, но на скорострельном автомате монтаж будет перезапускаться каждый выстрел.
- Пистолет в старой версии был **Pistol1H** (одной рукой). Если хочется двумя руками — `Pistol2H`.

### Отдача рук (раздел Recoil → Arms)

Повороты в градусах: Pitch, Yaw, Roll. Значения — как они реально работали в старой игре.

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Upper Arm Recoil | 1, -1, -2 | 4, 0, 0 | 2, -3, -5 |
| Lower Arm Recoil | -1, -1, 1 | 4, 3, 0 | -1, -1, 2 |
| Hand Recoil | 1, 2, 1 | 4, 3, 0 | 2, 4, 1 |
| Arm Recoil Scale | 1 | 1 | 1 |

- В старом `ABP_SandboxCharacter` предплечье и кисть были перепутаны местами (`LowerArm` брал значение из `Hand_R_Rotation` оружия и наоборот). В таблице это уже учтено, так что выглядеть будет так же, как раньше.
- Рука дёрнется слишком сильно или слабо — меняй **Arm Recoil Scale**. Хочешь другую форму рывка — создай Curve Float (время в секундах → 0..1) и поставь в **Arm Recoil Curve**.
- В прицеливании рывок умножается на **Aim Recoil Multiplier**.

### Стрельба и урон

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Fire Mode | Auto | Single | Single |
| Rounds Per Minute | 650 | 400 | 70 |
| Pellets Per Shot | 1 | 1 | 9 |
| Pellet Spread Degrees | — | — | 4 |
| Magazine Size | 30 | 12 | 6 |
| Starting / Max Reserve | 120 / 240 | 48 / 96 | 24 / 48 |
| Damage | 25 | 30 | 12 (на дробину) |
| Range | 15000 | 8000 | 4000 |
| Falloff Start / End | 3000 / 8000 | 1500 / 4000 | 800 / 2500 |
| Min Damage Multiplier | 0.5 | 0.5 | 0.2 |
| Hip / Aim Spread | 2.5 / 0.4 | 1.5 / 0.5 | 3 / 2 |
| Recoil Pitch | 0.6–0.9 | 1.2–1.6 | 3–4 |

### Общие поля

Одинаковые для всех трёх:

- **Fire Sound** — `SC_GunShot_Cue`.
- **Dry Fire Sound** — `SC_EmptyGunShot_Cue`.
- **Fire Camera Shake** — `BP_CameraShake_Recoil` (`Content/Blueprints/Cameras`).
- **Muzzle Flash** — `P_Muzzle` (Effects/MuzzleFlash) или `NS_WeaponFire` из Lyra.
- **Tracer** — `NS_WeaponFire_Tracer`, для дробовика `NS_WeaponFire_Tracer_Shotgun`. Точки попадания код передаёт сам.
- **Default Impact**:
  - Effect: `P_Concrete` (Effects/Particles/Hit).
  - Decal: `M_BulletHole` или `MI_Decal_Concrete`.
- **Surface Impacts** (по желанию): разные эффекты по типу поверхности, например Wood → `P_Wood`. Типы поверхностей задаются в Project Settings → Physics.

Если ствол сидит в руке криво, подстрой **Attach Offset**: Location и Rotation относительно сокета. Для оружия на спине — **Holster Offset**.

## 3. Персонаж (`CBP_SandboxCharacter`)

1. Убедись, что **Parent Class = NoFearCharacter** (File → Reparent Blueprint), и сохрани (Ctrl+S).
2. В Components выбери **WeaponComponent** и заполни:
   - **Starting Weapons**: `DA_Rifle`, `DA_Pistol`, `DA_Shotgun`.
   - **Input Mapping**: `IMC_Weapon`.
   - **Fire Action**: `IA_Fire`.
   - **Aim Action**: `IA_Aim`.
   - **Reload Action**: `IA_Reload`.
   - **Switch Weapon Action**: `IA_SwitchWeapon`.
   - **Holster Action**: `IA_Holster`.
3. Поза меняется сама: компонент пишет в переменную `OverlayState` и вызывает `OnRep_OverlayState`. Если появится предупреждение «нет позы …», проверь, что имя в Data Asset'е совпадает с пунктом `Enum_OverlayState`.
4. Раздел **Weapons → Animation** трогать не нужно: имена переменных (`Recoil_Amount`, `UpperArm_R_Rotation`, `LowerArm_R_Rotations`, `Hand_R_Rotations`) уже совпадают с `ABP_SandboxCharacter`.

## 4. Убрать «бутафорское» оружие оверлея

Оверлей GASP сам вешает в руку модельку (M4A1 / M9), и получилось бы два ствола. Открой:

- `OverlaySystem/Overlays/Rifle/DA_Overlay_Rifle`
- `OverlaySystem/Overlays/Pistol1H/DA_Overlay_Pistol1H`
- `OverlaySystem/Overlays/Pistol2H/DA_Overlay_Pistol2H`

В каждом очисти поле **Mesh** (или Static/Skeletal Mesh): кнопка ⟲ или выбрать None.

## 5. Починить `ABP_SandboxCharacter`

Анимблюпринт вернулся из старой версии. Слот `UpperBody` и узлы отдачи уже на месте. Сломались только узлы, которые читали старые `CBP_SandboxCharacter.Recoil_Amount` и `BP_Weapon`: этих классов больше нет, теперь значения пишет C++.

### 5.1. EventGraph: удалить старое чтение отдачи

1. Открой `ABP_SandboxCharacter` и нажми **Compile**. В Compiler Results будут ошибки про `Recoil_Amount`, `SelectedItemInHand` и `BP_Weapon`.
2. В **EventGraph** найди **Event Blueprint Update Animation** → Branch → **Sequence**.
3. Удали всё, что висит на выходах **Then 0** и **Then 1**:
   - Cast To CBP_SandboxCharacter;
   - два **Is Valid** (макрос с ромбиком);
   - Set **Recoil_Amount** и серый узел Recoil_Amount (из персонажа);
   - **Selected Item In Hand**;
   - Cast To **BP_Weapon**;
   - три Set: **UpperArm_R_Rotation**, **LowerArm_R_Rotations**, **Hand_R_Rotations**, и их геттеры.
4. **Then 2** (Update_CVarDrivenVariables → Update_Logic) не трогай.

Сами переменные `Recoil_Amount`, `UpperArm_R_Rotation`, `LowerArm_R_Rotations` и `Hand_R_Rotations` в панели **My Blueprint** **не удаляй**: в них пишет C++.

### 5.2. AnimGraph: включить отдачу всегда

Узлы отдачи стоят в ветке **True** у **Blend Poses by bool**. Условие у него «Is Slot Active (DefaultSlot) OR MovementMode == …», и при обычной стрельбе оно ложно. Поэтому раньше рука дёргалась не всегда.

1. В **AnimGraph** найди цепочку **Modify Bone** (upperarm_r → lowerarm_r → hand_r → spine_01 → spine_02 → spine_03). Она уходит в **True Pose** у Blend Poses by bool, а **False Pose** — это `Use cached pose 'PreHandIK'`.
2. У этого Blend Poses by bool отсоедини провод от **Active Value**: Alt+клик по пину.
3. Поставь галочку **Active Value** = true.
4. Узлы OR, Is Slot Active и сравнение enum'а можно удалить.

Без выстрела `Recoil_Amount` = 0, и узлы Modify Bone ничего не меняют. Поэтому ветка True в покое выглядит так же, как `PreHandIK`.

### 5.3. Сохранить

**Compile** — ошибок быть не должно. **Save**.

## 6. Проверка

| Клавиша | Что делает |
|---|---|
| ЛКМ | Огонь. Автомат стреляет, пока держишь, и сразу перестаёт, когда отпускаешь. Правая рука и корпус коротко дёргаются на каждом выстреле. |
| ПКМ | Прицел: разброс, отдача и рывок рук меньше. |
| R | Перезарядка с анимацией. При пустом магазине и зажатом огне — сама. |
| Колесо | Смена оружия. Убранные стволы висят на спине и на поясе. |
| X | Убрать / достать. |

Боты стреляют через тот же код: `WeaponComponent → StartFire / StopFire`. Точность бота берётся из `NoFearCombatComponent`. Рывок рук и оружие на спине у ботов тоже есть.
