#include "utility.h"
/*  VTC Blockindexer - A utility to build additional indexes to the 
    Vertcoin blockchain by scanning and indexing the blockfiles
    downloaded by Vertcoin Core.
    
    Copyright (C) 2017  Gert-Jaap Glasbergen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <openssl/sha.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <iomanip>
#include <vector>
#include <secp256k1.h>
#include "crypto/ripemd160.h"
#include "crypto/base58.h"
#include "crypto/bech32.h"


using namespace std;

namespace
{
    /* Global secp256k1_context object used for verification. */
    secp256k1_context* secp256k1_context_verify = NULL;

    typedef std::vector<uint8_t> data;

    template<int frombits, int tobits, bool pad>
    bool convertbits(data& out, const data& in) {
        int acc = 0;
        int bits = 0;
        const int maxv = (1 << tobits) - 1;
        const int max_acc = (1 << (frombits + tobits - 1)) - 1;
        for (size_t i = 0; i < in.size(); ++i) {
            int value = in[i];
            acc = ((acc << frombits) | value) & max_acc;
            bits += frombits;
            while (bits >= tobits) {
                bits -= tobits;
                out.push_back((acc >> bits) & maxv);
            }
        }
        if (pad) {
            if (bits) out.push_back((acc << (tobits - bits)) & maxv);
        } else if (bits >= frombits || ((acc << (tobits - bits)) & maxv)) {
            return false;
        }
        return true;
    }
}

vector<unsigned char> VtcBlockIndexer::Utility::sha256(vector<unsigned char> input)
{
    unique_ptr<unsigned char> hash(new unsigned char [SHA256_DIGEST_LENGTH]);

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, &input[0], input.size());
    SHA256_Final(hash.get(), &sha256);
    return vector<unsigned char>(hash.get(), hash.get()+SHA256_DIGEST_LENGTH);
}

std::string VtcBlockIndexer::Utility::hashToHex(vector<unsigned char> hash) {
    stringstream ss;
    for(uint i = 0; i < hash.size(); i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash.at(i);
    }
    return ss.str();
}

std::string VtcBlockIndexer::Utility::hashToReverseHex(vector<unsigned char> hash) {
    if(hash.size() == 0) return "";
    stringstream ss;
    for(uint i = hash.size(); i-- > 0;)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash.at(i);
    }
    return ss.str();
}

void VtcBlockIndexer::Utility::initECCContextIfNeeded() {
    if(secp256k1_context_verify == NULL) {
        secp256k1_context_verify = secp256k1_context_create(SECP256K1_FLAGS_TYPE_CONTEXT | SECP256K1_FLAGS_BIT_CONTEXT_VERIFY);
    }
}

VtcBlockIndexer::Utility::~Utility() {
    if(secp256k1_context_verify != NULL) {
        secp256k1_context_destroy(secp256k1_context_verify);
        secp256k1_context_verify = NULL;
    }
}

vector<unsigned char> VtcBlockIndexer::Utility::decompressPubKey(vector<unsigned char> compressedKey) {

    initECCContextIfNeeded();
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(secp256k1_context_verify, &pubkey, &compressedKey[0], 33)) {
        return {};
    }
    unique_ptr<unsigned char> pub(new unsigned char[65]);
    size_t publen = 65;
    secp256k1_ec_pubkey_serialize(secp256k1_context_verify, pub.get(), &publen, &pubkey, SECP256K1_EC_UNCOMPRESSED);

    
     vector<unsigned char> returnValue (pub.get(), pub.get() + 65);
     return returnValue;
}     


vector<unsigned char> VtcBlockIndexer::Utility::publicKeyToAddress(vector<unsigned char> publicKey, bool testnet) {
    vector<unsigned char> hashedKey = sha256(publicKey);
    vector<unsigned char> ripeMD = ripeMD160(hashedKey);
    return ripeMD160ToP2PKAddress(ripeMD, testnet);
}

vector<unsigned char> VtcBlockIndexer::Utility::ripeMD160ToP2PKAddress(vector<unsigned char> ripeMD, bool testnet) {
    return ripeMD160ToAddress(testnet ? 0x4A : 0x47, ripeMD);
}
vector<unsigned char> VtcBlockIndexer::Utility::ripeMD160ToP2SHAddress(vector<unsigned char> ripeMD, bool testnet) {
    return ripeMD160ToAddress(testnet ? 0xC4 : 0x05, ripeMD);
}

vector<unsigned char> VtcBlockIndexer::Utility::ripeMD160ToAddress(unsigned char versionByte, vector<unsigned char> ripeMD) {
    ripeMD.insert(ripeMD.begin(), versionByte);
    vector<unsigned char> doubleHashedRipeMD = sha256(sha256(ripeMD));
    for(int i = 0; i < 4; i++) {
        ripeMD.push_back(doubleHashedRipeMD.at(i));
    }
    return base58(ripeMD);
}

vector<unsigned char> VtcBlockIndexer::Utility::hexToBytes(const std::string hex) {
    vector<unsigned char> bytes;
  
    for (unsigned int i = 0; i < hex.length(); i += 2) {
      string byteString = hex.substr(i, 2);
      unsigned char byte = (unsigned char) strtol(byteString.c_str(), NULL, 16);
      bytes.push_back(byte);
    }
  
    return bytes;
}

vector<unsigned char> VtcBlockIndexer::Utility::ripeMD160(vector<unsigned char> in) {
    unsigned char hash[CRIPEMD160::OUTPUT_SIZE];
    CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
    return vector<unsigned char>(hash, hash + CRIPEMD160::OUTPUT_SIZE);
}


vector<unsigned char> VtcBlockIndexer::Utility::base58(vector<unsigned char> in) {
    
    std::unique_ptr<char> b58(new char[80]);
    size_t size = 80;
    if(!b58enc(b58.get(), &size, in.data(), in.size())) {
        return {};
    }
    else 
    {
        return vector<unsigned char>(b58.get(), b58.get() + size-1); // -1 strips trailing 0 byte
    }
}

vector<unsigned char> VtcBlockIndexer::Utility::bech32Address(vector<unsigned char> in, bool testnet) {
    vector<unsigned char> enc;
    enc.push_back(0); // witness version
    if(convertbits<8, 5, true>(enc, in)) {
        string address = bech32::Encode(testnet ? "tvtc" : "vtc", enc);
        return vector<unsigned char>(address.begin(), address.end());
    }
    else{
        return {};
    }
}

vector<VtcBlockIndexer::EsignatureTransaction> VtcBlockIndexer::Utility::parseEsignatureTransactions(VtcBlockIndexer::Block block,leveldb::DB* db, VtcBlockIndexer::ScriptSolver* scriptSolver, VtcBlockIndexer::MempoolMonitor* mempoolMonitor) {

    vector<VtcBlockIndexer::EsignatureTransaction> returnValue = {};
    for(VtcBlockIndexer::Transaction tx : block.transactions) {
        if(tx.outputs.size() == 4) {
            if(tx.outputs.at(1).value == 100 && tx.outputs.at(2).value == 0 && tx.outputs.at(2).script.at(0) == 0x6A) {
                vector<string> addresses = scriptSolver->getAddressesFromScript(tx.outputs.at(3).script);
                if(addresses.size() == 1 && addresses.at(0).compare("WxVSkmSUCUXFsnTRVdy5s2jtXXiwdjg75P") == 0) {
                    // This is a signature TX. Find out the "from" address.
                    stringstream txoAddrKey;
                    txoAddrKey << tx.inputs.at(0).txHash << setw(8) << setfill('0') << tx.inputs.at(0).txoIndex;
                    string address;
                    leveldb::Status s = db->Get(leveldb::ReadOptions(), txoAddrKey.str(), &address);
                    bool ok = s.ok();
                    if(!ok) {
                        address = mempoolMonitor->getTxoAddress(tx.inputs.at(0).txHash,  tx.inputs.at(0).txoIndex);
                        if(address.compare("") != 0) {
                            ok = true;
                        }
                    }
                    if(ok) {
                        vector<string> docAddresses = scriptSolver->getAddressesFromScript(tx.outputs.at(1).script);
                        if(docAddresses.size() == 1) {
                            EsignatureTransaction trans;
                            trans.fromAddress = address;
                            trans.toAddress = docAddresses.at(0);
                            trans.script = tx.outputs.at(2).script;
                            trans.txId = tx.txHash;
                            trans.time = block.time;
                            trans.height = block.height;
                            returnValue.push_back(trans);
                        }
                    } else {
                        cout << "TXO not found in esignature TX" << endl;
                    }
                }
            }
        }
    }
    return returnValue;
}


vector<VtcBlockIndexer::IdentityTransaction> VtcBlockIndexer::Utility::parseIdentityTransactions(VtcBlockIndexer::Block block,leveldb::DB* db, VtcBlockIndexer::ScriptSolver* scriptSolver, VtcBlockIndexer::MempoolMonitor* mempoolMonitor) {
    vector<VtcBlockIndexer::IdentityTransaction> returnValue = {};
    for(VtcBlockIndexer::Transaction tx : block.transactions) {
        if(tx.outputs.size() == 4) {
            if(tx.outputs.at(1).value == 100 && 
            tx.outputs.at(2).value == 0 && 
            tx.outputs.at(2).script.at(0) == 0x6A && 
            tx.outputs.at(2).script.at(1) == 0x04 && 
            tx.outputs.at(2).script.at(2) == 0x49 && 
            tx.outputs.at(2).script.at(3) == 0x44 && 
            tx.outputs.at(2).script.at(4) == 0x45 && 
            tx.outputs.at(2).script.at(5) == 0x4e && 
            tx.outputs.at(3).value == 0 && 
            tx.outputs.at(3).script.at(0) == 0x6A) {
                // This is an identity TX. Find out the "from" address.
                stringstream txoAddrKey;
                txoAddrKey << tx.inputs.at(0).txHash << setw(8) << setfill('0') << tx.inputs.at(0).txoIndex;
                string address;
                leveldb::Status s = db->Get(leveldb::ReadOptions(), txoAddrKey.str(), &address);
                bool ok = s.ok();
                if(!ok) {
                    address = mempoolMonitor->getTxoAddress(tx.inputs.at(0).txHash,  tx.inputs.at(0).txoIndex);
                    if(address.compare("") != 0) {
                        ok = true;
                    }
                }
                if(ok) {
                    vector<string> personAddress = scriptSolver->getAddressesFromScript(tx.outputs.at(1).script);
                    if(personAddress.size() == 1) {
                        IdentityTransaction trans;
                        trans.fromAddress = address;
                        trans.toAddress = personAddress.at(0);
                        trans.script = tx.outputs.at(3).script;
                        trans.txId = tx.txHash;
                        trans.time = block.time;
                        trans.height = block.height;
                        returnValue.push_back(trans);
                    }
                } else {
                    cout << "TXO not found in esignature TX" << endl;
                }
            }
        }
    }
    return returnValue;
}