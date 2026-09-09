# cguid Class Reference

The `cguid` class provides handling for VSCP GUIDs (Globally Unique Identifiers). A VSCP GUID is a 16-byte identifier used to uniquely identify nodes, interfaces, and other entities in the VSCP system.

## Header

```cpp
#include "guid.h"
```

## Overview

The GUID is stored as a 16-byte array with MSB at index 0 and LSB at index 15. The class supports multiple string formats for parsing and output:

- **Standard format**: `FF:EE:DD:CC:BB:AA:99:88:77:66:55:44:33:22:11:00`
- **UUID format**: `FFEEDDCC-BBAA-9988-7766-554433221100`
- **Compact format**: `::11:22:33:44:55:66` (leading 0xFF bytes compressed)
- **Zero prefix**: `-:11:22:33:44:55:66` (leading zeros)
- **Braces**: `{FF:EE:DD:CC:...}`

## GUID Byte Layout

| Index | Description |
|-------|-------------|
| 0 | MSB (Most Significant Byte) |
| 12-13 | Client ID (daemon use) |
| 14-15 | Nickname ID |
| 15 | LSB (Least Significant Byte) / Nickname |

---

## Constructors

### Default Constructor
```cpp
cguid();
```
Creates an empty GUID (all bytes set to zero).

### Array Constructor
```cpp
cguid(const uint8_t *pguid);
```
Creates a GUID from a 16-byte array.

**Parameters:**
- `pguid` - Pointer to a 16-byte array containing the GUID in MSB→LSB order.

### Copy Constructor
```cpp
cguid(const cguid &guid);
```
Creates a copy of another cguid instance.

**Parameters:**
- `guid` - The cguid instance to copy.

### String Constructor
```cpp
cguid(const std::string &strGUID);
```
Creates a GUID by parsing a string. Supports multiple formats including standard colon-separated, UUID format, compact notation (`::` for 0xFF, `-:` for zeros), and braces.

**Parameters:**
- `strGUID` - GUID string in any supported format.

**Examples:**
```cpp
cguid guid1("FF:EE:DD:CC:BB:AA:99:88:77:66:55:44:33:22:11:00");
cguid guid2("FFEEDDCC-BBAA-9988-7766-554433221100");  // UUID format
cguid guid3("::");                                     // All 0xFF
cguid guid4("::01:02:03");                            // Leading 0xFF
cguid guid5("-:01:02:03");                            // Leading zeros
```

---

## Operators

### Assignment Operator
```cpp
cguid& operator=(const cguid &guid);
```
Assigns another GUID to this instance.

### Equality Operator
```cpp
bool operator==(const cguid &guid);
```
Returns `true` if both GUIDs are identical.

### Inequality Operator
```cpp
bool operator!=(const cguid &guid);
```
Returns `true` if GUIDs are different.

### Index Operator (Read)
```cpp
uint8_t operator[](uint8_t pos) const;
```
Returns the byte at position `pos` (0-15). Position is masked with 0x0F.

### Index Operator (Write)
```cpp
uint8_t& operator[](uint8_t pos);
```
Returns a reference to the byte at position `pos` for modification.

---

## String Parsing Methods

### getFromString
```cpp
void getFromString(const std::string &strGUID);
void getFromString(const char *pszGUID);
```
Parses a GUID string and stores the result. Supports all string formats.

**Parameters:**
- `strGUID` / `pszGUID` - GUID string to parse.

**Supported formats:**
- Standard: `FF:EE:DD:CC:BB:AA:99:88:77:66:55:44:33:22:11:00`
- UUID: `FFEEDDCC-BBAA-9988-7766-554433221100`
- Compact (all 0xFF): `::`
- Compact (leading 0xFF): `::01:02:03`
- Zero prefix (leading zeros): `-:01:02:03`
- Multi-byte values: `::0102:03aa:bb0c`
- With braces: `{FF:EE:DD:...}`

### getFromArray
```cpp
void getFromArray(const uint8_t *pguid);
```
Copies a 16-byte array into the GUID.

**Parameters:**
- `pguid` - Pointer to 16-byte array in MSB→LSB order.

---

## String Output Methods

### toString
```cpp
void toString(std::string &strGUID);
std::string toString(void);
```
Converts the GUID to standard colon-separated uppercase hex format.

**Returns:** String like `FF:EE:DD:CC:BB:AA:99:88:77:66:55:44:33:22:11:00`

### toStringCompact
```cpp
void toStringCompact(std::string &strGUID);
std::string toStringCompact(void);
```
Converts the GUID to compact format, replacing leading 0xFF bytes with `::`.

**Examples:**
```cpp
cguid guid("FF:FF:FF:FF:FF:FF:FF:FF:FF:FF:11:22:33:44:55:66");
std::string str = guid.toStringCompact();  // "::11:22:33:44:55:66"

cguid guid2("::");
std::string str2 = guid2.toStringCompact();  // "::"

cguid guid3("01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10");
std::string str3 = guid3.toStringCompact();  // "01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10"
```

### toStringUUID
```cpp
void toStringUUID(std::string &strGUID);
std::string toStringUUID(void);
```
Converts the GUID to UUID format (8-4-4-4-12 hex digits with dashes).

**Returns:** String like `FFEEDDCC-BBAA-9988-7766-554433221100`

### getAsString
```cpp
std::string getAsString(void);
```
Alias for `toString()`. Returns the GUID in standard format.

---

## Byte Access Methods

### clear
```cpp
void clear(void);
```
Sets all 16 bytes of the GUID to zero.

### getGUID
```cpp
const uint8_t* getGUID(void);
uint8_t getGUID(const unsigned char n);
```
Returns a pointer to the internal 16-byte array, or the byte at position `n`.

### getAt
```cpp
uint8_t getAt(const unsigned char n);
```
Returns the byte at position `n` (0-15). Position is masked with 0x0F.

### setAt
```cpp
void setAt(const unsigned char n, const unsigned char value);
```
Sets the byte at position `n` to `value`. Position is masked with 0x0F.

### getLSB / setLSB
```cpp
uint8_t getLSB(void);
void setLSB(const unsigned char value);
```
Gets/sets the LSB (byte at index 15), which is also the nickname byte.

### getMSB / setMSB
```cpp
uint8_t getMSB(void);
void setMSB(const unsigned char value);
```
Gets/sets the MSB (byte at index 0).

---

## Write Methods

### writeGUID
```cpp
void writeGUID(uint8_t *pArray);
```
Copies the GUID to an external array in MSB→LSB order (standard).

**Parameters:**
- `pArray` - Pointer to a 16-byte destination array.

### writeGUID_reverse
```cpp
void writeGUID_reverse(uint8_t *pArray);
```
Copies the GUID to an external array in LSB→MSB order (reversed).

**Parameters:**
- `pArray` - Pointer to a 16-byte destination array.

---

## Comparison Methods

### isSameGUID
```cpp
bool isSameGUID(const unsigned char *pguid);
```
Compares this GUID with a 16-byte array.

**Parameters:**
- `pguid` - Pointer to a 16-byte array to compare.

**Returns:** `true` if identical, `false` otherwise. Returns `false` if `pguid` is `nullptr`.

### isNULL
```cpp
bool isNULL(void);
```
Checks if all 16 bytes of the GUID are zero.

**Returns:** `true` if all bytes are zero.

---

## VSCP-Specific Methods

### setClientID / getClientID
```cpp
void setClientID(uint16_t clientid);
uint16_t getClientID(void);
```
Gets/sets the client ID stored in bytes 12-13 (used by VSCP MQTT daemon).

### setNicknameID / getNicknameID
```cpp
void setNicknameID(uint16_t nicknameid);
uint16_t getNicknameID(void);
```
Gets/sets the 16-bit nickname ID stored in bytes 14-15.

### getNickname
```cpp
uint8_t getNickname(void);
```
Returns the 8-bit nickname (byte at index 15, same as LSB).

---

## Member Variables

### m_id
```cpp
uint8_t m_id[16];
```
The 16-byte GUID array. Index 0 is MSB, index 15 is LSB. Public member for direct access when needed.

---

## Example Usage

```cpp
#include "guid.h"
#include <iostream>

int main() {
    // Create from string
    cguid guid("FF:EE:DD:CC:BB:AA:99:88:77:66:55:44:33:22:11:00");
    
    // Output in different formats
    std::cout << "Standard: " << guid.toString() << std::endl;
    std::cout << "UUID:     " << guid.toStringUUID() << std::endl;
    std::cout << "Compact:  " << guid.toStringCompact() << std::endl;
    
    // Modify bytes
    guid.setLSB(0x42);
    guid.setNicknameID(0x1234);
    
    // Compare
    cguid guid2 = guid;
    if (guid == guid2) {
        std::cout << "GUIDs are equal" << std::endl;
    }
    
    // Check for null
    cguid empty;
    if (empty.isNULL()) {
        std::cout << "GUID is null" << std::endl;
    }
    
    // Parse compact format
    cguid compact("::01:02:03");  // Leading bytes are 0xFF
    
    return 0;
}
```

---

## See Also

- [vscp-guid-parser.h](../src/vscp/common/vscp-guid-parser.h) - Low-level C GUID parsing functions
- [VSCP Specification](https://docs.vscp.org/) - Full VSCP protocol documentation
