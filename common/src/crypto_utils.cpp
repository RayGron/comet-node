#include "comet/crypto_utils.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

namespace comet {

namespace {

std::once_flag g_crypto_init_once;

void EnsureCrypto() {
  std::call_once(g_crypto_init_once, []() {
    OpenSSL_add_all_algorithms();
  });
}

std::string EncodeBase64(const unsigned char* data, std::size_t size) {
  const std::size_t encoded_size = 4 * ((size + 2) / 3);
  std::string out(encoded_size, '\0');
  const int written = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(out.data()),
      data,
      static_cast<int>(size));
  if (written < 0) {
    throw std::runtime_error("failed to encode base64");
  }
  out.resize(static_cast<std::size_t>(written));
  return out;
}

std::vector<unsigned char> DecodeBase64(const std::string& text, const std::string& label) {
  if (text.empty()) {
    return {};
  }

  std::string normalized = text;
  while (!normalized.empty() && normalized.back() == '=') {
    normalized.pop_back();
  }

  std::vector<unsigned char> output((normalized.size() * 3) / 4 + 3, 0);
  const int decoded = EVP_DecodeBlock(
      output.data(),
      reinterpret_cast<const unsigned char*>(text.data()),
      static_cast<int>(text.size()));
  if (decoded < 0) {
    throw std::runtime_error("failed to decode base64 " + label);
  }

  std::size_t actual_size = static_cast<std::size_t>(decoded);
  std::size_t padding = 0;
  for (auto it = text.rbegin(); it != text.rend() && *it == '='; ++it) {
    ++padding;
  }
  if (padding > actual_size) {
    throw std::runtime_error("invalid base64 padding for " + label);
  }
  actual_size -= padding;
  output.resize(actual_size);
  return output;
}

std::string ToHex(const unsigned char* data, std::size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < size; ++index) {
    out << std::setw(2) << static_cast<int>(data[index]);
  }
  return out.str();
}

class EvpPkeyHandle {
 public:
  explicit EvpPkeyHandle(EVP_PKEY* key) : key_(key) {}
  ~EvpPkeyHandle() {
    if (key_ != nullptr) {
      EVP_PKEY_free(key_);
    }
  }

  EvpPkeyHandle(const EvpPkeyHandle&) = delete;
  EvpPkeyHandle& operator=(const EvpPkeyHandle&) = delete;

  EVP_PKEY* get() const { return key_; }

 private:
  EVP_PKEY* key_ = nullptr;
};

EVP_PKEY* LoadPrivateKey(const std::string& private_key_base64) {
  const auto private_key_bytes = DecodeBase64(private_key_base64, "private key");
  const unsigned char* input = private_key_bytes.data();
  return d2i_PrivateKey(EVP_PKEY_ED25519, nullptr, &input, private_key_bytes.size());
}

EVP_PKEY* LoadPublicKey(const std::string& public_key_base64) {
  const auto public_key_bytes = DecodeBase64(public_key_base64, "public key");
  const unsigned char* input = public_key_bytes.data();
  return d2i_PUBKEY(nullptr, &input, public_key_bytes.size());
}

std::array<unsigned char, SHA256_DIGEST_LENGTH> DeriveEnvelopeKey(
    const std::string& shared_secret_base64) {
  const auto secret = DecodeBase64(shared_secret_base64, "shared secret");
  std::array<unsigned char, SHA256_DIGEST_LENGTH> key{};
  SHA256(secret.data(), secret.size(), key.data());
  return key;
}

}  // namespace

void InitializeCrypto() {
  EnsureCrypto();
}

SigningKeypair GenerateSigningKeypair() {
  EnsureCrypto();
  EVP_PKEY_CTX* raw_context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (raw_context == nullptr) {
    throw std::runtime_error("failed to create key generation context");
  }
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
      raw_context,
      &EVP_PKEY_CTX_free);
  if (EVP_PKEY_keygen_init(context.get()) <= 0) {
    throw std::runtime_error("failed to initialize Ed25519 key generation");
  }
  EVP_PKEY* generated_key = nullptr;
  if (EVP_PKEY_keygen(context.get(), &generated_key) <= 0 || generated_key == nullptr) {
    throw std::runtime_error("failed to generate signing keypair");
  }
  EvpPkeyHandle key(generated_key);

  int private_length = i2d_PrivateKey(key.get(), nullptr);
  int public_length = i2d_PUBKEY(key.get(), nullptr);
  if (private_length <= 0 || public_length <= 0) {
    throw std::runtime_error("failed to encode generated signing keypair");
  }

  std::vector<unsigned char> private_key(static_cast<std::size_t>(private_length));
  std::vector<unsigned char> public_key(static_cast<std::size_t>(public_length));
  unsigned char* private_ptr = private_key.data();
  unsigned char* public_ptr = public_key.data();
  if (i2d_PrivateKey(key.get(), &private_ptr) != private_length ||
      i2d_PUBKEY(key.get(), &public_ptr) != public_length) {
    throw std::runtime_error("failed to serialize generated signing keypair");
  }

  return SigningKeypair{
      EncodeBase64(public_key.data(), public_key.size()),
      EncodeBase64(private_key.data(), private_key.size()),
  };
}

std::string ComputeKeyFingerprintHex(const std::string& public_key_base64) {
  EnsureCrypto();
  const auto public_key = DecodeBase64(public_key_base64, "public key");
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(public_key.data(), public_key.size(), digest);
  return ToHex(digest, sizeof(digest));
}

std::string RandomTokenBase64(int byte_count) {
  EnsureCrypto();
  if (byte_count <= 0) {
    throw std::runtime_error("byte_count must be positive");
  }
  std::vector<unsigned char> bytes(static_cast<std::size_t>(byte_count));
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    throw std::runtime_error("failed to generate random bytes");
  }
  return EncodeBase64(bytes.data(), bytes.size());
}

std::string SignDetachedBase64(
    const std::string& message,
    const std::string& private_key_base64) {
  EnsureCrypto();
  EvpPkeyHandle private_key(LoadPrivateKey(private_key_base64));
  if (private_key.get() == nullptr) {
    throw std::runtime_error("failed to parse private key");
  }

  EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
  if (raw_context == nullptr) {
    throw std::runtime_error("failed to create signing context");
  }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      raw_context,
      &EVP_MD_CTX_free);

  if (EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, private_key.get()) <= 0) {
    throw std::runtime_error("failed to initialize signing context");
  }

  std::size_t signature_size = 0;
  if (EVP_DigestSign(
          context.get(),
          nullptr,
          &signature_size,
          reinterpret_cast<const unsigned char*>(message.data()),
          message.size()) <= 0) {
    throw std::runtime_error("failed to size signature");
  }

  std::vector<unsigned char> signature(signature_size);
  if (EVP_DigestSign(
          context.get(),
          signature.data(),
          &signature_size,
          reinterpret_cast<const unsigned char*>(message.data()),
          message.size()) <= 0) {
    throw std::runtime_error("failed to sign message");
  }
  signature.resize(signature_size);
  return EncodeBase64(signature.data(), signature_size);
}

bool VerifyDetachedBase64(
    const std::string& message,
    const std::string& signature_base64,
    const std::string& public_key_base64) {
  EnsureCrypto();
  EvpPkeyHandle public_key(LoadPublicKey(public_key_base64));
  if (public_key.get() == nullptr) {
    return false;
  }
  const auto signature = DecodeBase64(signature_base64, "signature");

  EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
  if (raw_context == nullptr) {
    throw std::runtime_error("failed to create verify context");
  }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      raw_context,
      &EVP_MD_CTX_free);

  if (EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, public_key.get()) <= 0) {
    return false;
  }
  return EVP_DigestVerify(
             context.get(),
             signature.data(),
             signature.size(),
             reinterpret_cast<const unsigned char*>(message.data()),
             message.size()) == 1;
}

EncryptedEnvelope EncryptEnvelopeBase64(
    const std::string& plaintext,
    const std::string& shared_secret_base64,
    const std::string& aad) {
  EnsureCrypto();
  const auto key = DeriveEnvelopeKey(shared_secret_base64);
  std::array<unsigned char, 12> nonce{};
  if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
    throw std::runtime_error("failed to generate envelope nonce");
  }

  EVP_CIPHER_CTX* raw_context = EVP_CIPHER_CTX_new();
  if (raw_context == nullptr) {
    throw std::runtime_error("failed to create encryption context");
  }
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(
      raw_context,
      &EVP_CIPHER_CTX_free);

  if (EVP_EncryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) !=
      1) {
    throw std::runtime_error("failed to initialize envelope cipher");
  }
  if (EVP_CIPHER_CTX_ctrl(
          context.get(),
          EVP_CTRL_AEAD_SET_IVLEN,
          static_cast<int>(nonce.size()),
          nullptr) != 1) {
    throw std::runtime_error("failed to configure envelope nonce size");
  }
  if (EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
    throw std::runtime_error("failed to initialize envelope key");
  }

  int out_length = 0;
  if (!aad.empty() &&
      EVP_EncryptUpdate(
          context.get(),
          nullptr,
          &out_length,
          reinterpret_cast<const unsigned char*>(aad.data()),
          static_cast<int>(aad.size())) != 1) {
    throw std::runtime_error("failed to apply envelope aad");
  }

  std::vector<unsigned char> ciphertext(plaintext.size());
  if (!plaintext.empty() &&
      EVP_EncryptUpdate(
          context.get(),
          ciphertext.data(),
          &out_length,
          reinterpret_cast<const unsigned char*>(plaintext.data()),
          static_cast<int>(plaintext.size())) != 1) {
    throw std::runtime_error("failed to encrypt envelope payload");
  }
  const int ciphertext_size = out_length;

  int final_length = 0;
  if (EVP_EncryptFinal_ex(context.get(), ciphertext.data() + ciphertext_size, &final_length) != 1) {
    throw std::runtime_error("failed to finalize envelope encryption");
  }
  ciphertext.resize(static_cast<std::size_t>(ciphertext_size + final_length));

  std::array<unsigned char, 16> tag{};
  if (EVP_CIPHER_CTX_ctrl(
          context.get(),
          EVP_CTRL_AEAD_GET_TAG,
          static_cast<int>(tag.size()),
          tag.data()) != 1) {
    throw std::runtime_error("failed to read envelope authentication tag");
  }
  ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());

  return EncryptedEnvelope{
      EncodeBase64(nonce.data(), nonce.size()),
      EncodeBase64(ciphertext.data(), ciphertext.size()),
  };
}

std::string DecryptEnvelopeBase64(
    const EncryptedEnvelope& envelope,
    const std::string& shared_secret_base64,
    const std::string& aad) {
  EnsureCrypto();
  const auto key = DeriveEnvelopeKey(shared_secret_base64);
  const auto nonce = DecodeBase64(envelope.nonce_base64, "envelope nonce");
  auto ciphertext = DecodeBase64(envelope.ciphertext_base64, "envelope ciphertext");
  if (nonce.empty() || ciphertext.size() < 16) {
    throw std::runtime_error("invalid encrypted envelope");
  }

  const std::size_t tag_offset = ciphertext.size() - 16;
  std::array<unsigned char, 16> tag{};
  std::copy(ciphertext.begin() + static_cast<std::ptrdiff_t>(tag_offset), ciphertext.end(), tag.begin());
  ciphertext.resize(tag_offset);

  EVP_CIPHER_CTX* raw_context = EVP_CIPHER_CTX_new();
  if (raw_context == nullptr) {
    throw std::runtime_error("failed to create decryption context");
  }
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(
      raw_context,
      &EVP_CIPHER_CTX_free);

  if (EVP_DecryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) !=
      1) {
    throw std::runtime_error("failed to initialize envelope decrypt cipher");
  }
  if (EVP_CIPHER_CTX_ctrl(
          context.get(),
          EVP_CTRL_AEAD_SET_IVLEN,
          static_cast<int>(nonce.size()),
          nullptr) != 1) {
    throw std::runtime_error("failed to configure decrypt nonce size");
  }
  if (EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
    throw std::runtime_error("failed to initialize decrypt key");
  }

  int out_length = 0;
  if (!aad.empty() &&
      EVP_DecryptUpdate(
          context.get(),
          nullptr,
          &out_length,
          reinterpret_cast<const unsigned char*>(aad.data()),
          static_cast<int>(aad.size())) != 1) {
    throw std::runtime_error("failed to apply decrypt aad");
  }

  std::vector<unsigned char> plaintext(ciphertext.size());
  if (!ciphertext.empty() &&
      EVP_DecryptUpdate(
          context.get(),
          plaintext.data(),
          &out_length,
          ciphertext.data(),
          static_cast<int>(ciphertext.size())) != 1) {
    throw std::runtime_error("failed to decrypt envelope payload");
  }
  const int plaintext_size = out_length;

  if (EVP_CIPHER_CTX_ctrl(
          context.get(),
          EVP_CTRL_AEAD_SET_TAG,
          static_cast<int>(tag.size()),
          tag.data()) != 1) {
    throw std::runtime_error("failed to set decrypt authentication tag");
  }

  int final_length = 0;
  if (EVP_DecryptFinal_ex(context.get(), plaintext.data() + plaintext_size, &final_length) != 1) {
    throw std::runtime_error("failed to verify encrypted envelope");
  }
  plaintext.resize(static_cast<std::size_t>(plaintext_size + final_length));
  return std::string(plaintext.begin(), plaintext.end());
}

}  // namespace comet
