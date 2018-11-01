# Syntax  
  
- `[BLOCK_ID]`: a NAND page number, computed from an address by ignoring the lowest `blockOffsetBits` bits. The `USE_BLOCK` instruction may cause this information not to be explicitely encoded, as it is then be implied by the preceding `USE_BLOCK`. Only the first BlockID may then be skipped using this approach. Although by default encoded in `blockIDBits` bits, the encoded length can be shortened by the use of the `REBASE` instruction's second argument;  
  
- `[Length]`: 	An integer of length `lengthBits`, that must be ≥ to `blockOffsetBits`. Used to encode length of copies; 
  
- `[Offset]`:	An integer of length `blockOffsetBits`. Used to encode NAND page offsets;  
  
- `[Length-x]`:	An integer of length `#x` bits. Used when numbers smaller than a full length have to be encoded.  
  
# Instructions encoding and features  


| Instruction | Encoding | Features |
| ------------- | ------------- | ------------- |
| `ERASE`  | `[OPCODE_ERASE].[BLOCK_ID]`  | Erase NAND page `#BlockID` |
| `LOAD_AND_FLUSH` | `[OPCODE_LOAD_FLUSH].[BLOCK_ID]` | Load the content of NAND page `#BlockID` to the cache then erase the NAND page   |
| `COMMIT` | `[OPCODE_COMMIT].[BLOCK_ID]` | Write the content of the cache to the NAND page `#BlockID`   |
| `FLUSH_AND_PARTIAL_COMMIT` | `[OPCODE_FLUSH_COMMIT].[BLOCK_ID].[Length]` | Erase NAND page `#BlockID` then write the `#Length` first bytes of the cache to it   |
| `USE_BLOCK` | `[OPCODE_USE_BLOCK].[BLOCK_ID]` | Tell the operation decoder that from now on, `#BlockID` is implied and won't be encoded<br>For operations involving multiple BlockIDs (copies), only the first BlockID is implied  |
| `RELEASE_BLOCK` | `[OPCODE_RELEASE]` | Tell the operation decoder that BlockIDs are no longer implied   |
| `REBASE` | `[OPCODE_REBASE].[BLOCK_ID].[Length-REBASE_LENGTH_BITS]` | Tell the operation decoder that from now on, decoded BlockIDs will be shifted by `#BlockID`<br>Also tells that BlockID's encoded length will be shortened to `#Length` bits<br>Note: This encoding of `#BlockID` is the only one unaffected by USE_BLOCK |
| `COPY_NAND_TO_NAND` | `[OPCODE_COPY_NN].[BLOCK_ID (1)].[Offset (1)].[Length].[BLOCK_ID (2)].[Offset (2)]` | Copy `#Length` bytes from NAND page `#BlockID (1)` at offset `#Offset (1)` to NAND page `#BlockID (2)` at offset `#Offset (2)`<br>`#BlockID (1)` will be implied if USE_BLOCK is in use.|
| `COPY_NAND_TO_CACHE` | `[OPCODE_COPY_NC].[BLOCK_ID (1)].[Offset (1)].[Length].[Offset (2)]` | Copy `#Length` bytes from NAND page `#BlockID (1)` at offset `#Offset (1)` to the cache at offset `#Offset (2)`   |
| `COPY_CACHE_TO_NAND` | `[OPCODE_COPY_CN].[Offset (1)].[Length].[BLOCK_ID (2)].[Offset (2)]` | Copy `#Length` bytes from the cache at offset `#Offset (1)` to NAND page `#BlockID (2)` at offset `#Offset (2)`   |
| `COPY_CACHE_TO_CACHE` | `[OPCODE_COPY_CC].[Offset (1)].[Length].[Offset (2)]` | Copy `#Length` bytes from the cache at offset `#Offset (1)` to the cache at offset `#Offset (2)`   |
| `CHAINED_COPY_FROM_NAND` | `[OPCODE_CHAINED_COPY_N].[BLOCK_ID].[Offset].[Length]` | Copy `#Length` bytes from NAND page `#BlockID` at offset `#Offset` to the address the previous operation finished writting   |
| `CHAINED_COPY_FROM_CACHE` | `[OPCODE_CHAINED_COPY_C].[Offset].[Length]` | Copy `#Length` bytes from the cache at offset `#Offset` to the address the previous operation finished writting   |
| `CHAINED_COPY_SKIP` | `[OPCODE_CHAINED_SKIP].[Length-shortLengthBits]` | Advance the address at which the next chained copy will start writting by `#Length` bytes   |
| `END_OF_STREAM` | `[OPCODE_END_OF_STREAM]` (repeated) | Signal the end of the instruction stream   |