# SCADA ventilaatori juhtimine kütteseadme jahutamiseks

IAS1420 — *Arvutite ja süsteemide projekt*  
Autorid: _[siia nimed]_  
Juhendaja: **Andres Rähni**

Projektis realiseeriti ventilaatori juhtimissüsteem kütteseadme jahutamiseks, kasutades
**Arduino** mikrokontrollerit ja **Ignition Maker Edition** SCADA platvormi. SCADA suhtleb
kontrolleriga üle **Modbus TCP** ning võimaldab ventilaatorit jälgida ja juhtida
HMI kasutajaliidese kaudu.

## Funktsionaalsus

- Kütteseadme temperatuuri mõõtmine temperatuurianduri abil  
- Ventilaatori töö kahes režiimis:
  - **Automaatrežiim** – SCADA-s määratud temperatuuriseadistuse järgi reguleeritakse
    ventilaatori kiirust vastavalt mõõdetud temperatuurile
  - **Käsirežiim** – ventilaatori kiirus on konstantne ja seda muudetakse
    potentsiomeetriga (SCADA-s ainult jälgimine)
- Ventilaatori PWM-väljundi juhtimine Arduinost
- Andmevahetus SCADA ↔ Arduino üle **Modbus TCP**
- SCADA HMI:
  - töörežiimi valik (Auto / Manual)
  - temperatuurinäit ja ventilaatori kiirus
  - kütteseadme ja ventilaatori oleku visualiseerimine

## Kasutatud riistvara

- Arduino (UNO / Nano / muu – täpsusta)
- Kütteseade (12 V L7812CV pingeregulaator koormusega)
- Temperatuuriandur (täpsusta tüüp, nt NTC, LM35 vms)
- DC ventilaator (12 V)
- Potentsiomeeter ventilaatori käsijuhtimiseks
- Toiteallikas ja vajalikud lisakomponendid (takistid, dioodid jne)

## Kasutatud tarkvara ja protokollid

- **Ignition Maker Edition** – SCADA platvorm ja HMI
- **Arduino IDE** – mikrokontrolleri programmi arendus
- **Modbus TCP** – andmevahetus SCADA ja Arduino vahel
- (soovi korral) Git / GitHub versioonihalduseks

## Projekti struktuur

```text
.
├─ arduino/          # Arduino kood (sketch, teegid)
├─ ignition/         # Ignition SCADA projekti eksport (zip või .json)
├─ docs/             # Skeemid, aruande lisad, joonised
└─ README.md
