```
"firstReply":
{
	"signature" : "64B raw signature of reply",
	"challenge" : "8B",
	"updateKey" : "32B Ed25519 public key",
	"versionID" : "4B",
	"isCritical": "1B, will update flags for the bootloader before any destructive operation, imply layoutManipulation will protect the network stack",
	"wantHash"	: "1B, the number of hash ranges",
	"hashRanges": 
	[
		{
			"start"	: "4B address",
			"length": "2B"
		}
	]
}

"mainManifest" :
{
	"signature": "each bloc is signed with the key transfered in firstReply",

	"payload":
	{
		"__info" : "payload is compressed, likely using something based on lzfx",
		"decompressedPayload" :
		{
			"layoutManipulation":
			{
				"nbManipulations": "2B",
				"manipulations":
				[
					{
						"start" : "4B",
						"length": "2B",
						"dest"	: "4B"
					}
				]
			},

			"wasCritical":
			{
				"__info": "only if isCritical was set, signal where is the network stack to use/the instruction to roll back the changes in case of power failure. The flags set there will be reset after validation",
				"newBaseAddress" : "4B"
			},

			"bindiff":
			{
				"nbBinDiff" : "2B",
				"bindiff" :
				[
					{
						"__info" : "this structure is designed to make this chunk streamable",

						"lengthToAddToExisting" : "2B",
						"dataToAddToExisting" : "binary blob",
						"lengthToInsertAfterward" : "2B",
						"dataToInsert" : "binary blob",
						"jump" : "2B signed"
					}
				]
			},

			"validation":
			{
				"nbValidations" : "2B",
				"validations" :
				[
					{
						"startToHash" : "4B",
						"length" : "2B",
						"expectedHash" : "32B (SHA-256)"
					}
				]
			}
		}
	}
}
```
