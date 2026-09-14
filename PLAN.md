Using an RPC endpoint or an indexer to read Tezos data in openFrameworks is a great, lightweight approach.Since you only need to read data, you should avoid querying a raw Tezos Node RPC directly if possible. Native nodes return data in Michelson (the low-level language of Tezos smart contracts), which is difficult to parse in C++.Instead, you should query TzKT (api.tzkt.io). TzKT is a public, free Tezos indexer that automatically converts complex blockchain data into clean, human-readable JSON.  Here is how you can implement this in your openFrameworks app using ofxHTTP and JSON.1. Identify Your API EndpointsDepending on the trigger for your openFrameworks app, you will likely hit one of these three TzKT endpoints:Account Balance: GET [https://api.tzkt.io/v1/accounts/](https://api.tzkt.io/v1/accounts/){address}Returns account details. The balance field is in mutez (1 Tezos = 1,000,000 mutez).Token/NFT Balances: GET [https://api.tzkt.io/v1/tokens/balances?account=](https://api.tzkt.io/v1/tokens/balances?account=){address}&token.contract={contract_address}Great for checking if a user holds a specific NFT (like an fxhash piece) to unlock a visual state.  Contract Storage: GET [https://api.tzkt.io/v1/contracts/](https://api.tzkt.io/v1/contracts/){contract_address}/storageReturns the live variables stored in a smart contract, automatically formatted as JSON.  2. C++ Implementation (ofxHTTP + ofJson)Modern openFrameworks (0.10.0+) includes ofJson built-in (based on the excellent nlohmann::json library), which removes the need for ofxJSON.Here is a practical example of querying an account balance and parsing it.C++#include "ofMain.h"
#include "ofxHTTP.h"

// Note: Ensure this function is NOT called directly inside your main update() 
// or draw() loop, as synchronous HTTP requests will freeze your framerate. 
void fetchTezosData(std::string walletAddress) {
    
    // 1. Setup the URL
    std::string url = "https://api.tzkt.io/v1/accounts/" + walletAddress;
    
    // 2. Initialize ofxHTTP objects
    ofxHTTP::Client client;
    ofxHTTP::GetRequest request(url);
    
    try {
        // 3. Execute the request
        auto response = client.execute(request);
        
        // 4. Check for HTTP 200 OK
        if (response->getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
            
            // 5. Extract body text and parse to ofJson
            std::string responseBody = response->getBody().getText();
            ofJson json = ofJson::parse(responseBody);
            
            // 6. Extract data safely
            if (json.contains("balance")) {
                // TzKT returns balances as integers representing mutez
                long long mutezBalance = json["balance"].get<long long>();
                
                // Convert to standard Tezos (XTZ)
                float tezBalance = mutezBalance / 1000000.0f; 
                
                ofLogNotice("Tezos") << "Wallet Balance: " << tezBalance << " ꜩ";
                
                // TODO: Pass this variable to your generative system (e.g., scale a mesh based on balance)
            }
            
        } else {
            ofLogError("Tezos") << "HTTP Error: " << response->getStatus() << " " << response->getReason();
        }
        
    } catch (const Poco::Exception& exc) {
        ofLogError("Network") << "Connection Failed: " << exc.displayText();
    } catch (const ofJson::exception& e) {
        ofLogError("JSON") << "Parse error: " << e.what();
    }
}
3. Parsing Complex Array Data (NFTs/Tokens)If you query [https://api.tzkt.io/v1/tokens/balances](https://api.tzkt.io/v1/tokens/balances), TzKT returns a JSON array rather than a single object. You can iterate through it like this:C++ofJson jsonArray = ofJson::parse(responseBody);

// Ensure the response is an array
if (jsonArray.is_array()) {
    for (auto& tokenRecord : jsonArray) {
        std::string balance = tokenRecord["balance"].get<std::string>();
        std::string tokenId = tokenRecord["token"]["tokenId"].get<std::string>();
        
        ofLogNotice("NFT") << "Holds " << balance << " of token ID " << tokenId;
    }
}
(Note: Token balances are often returned as strings by indexing APIs because blockchain numbers can exceed C++ standard integer limits, so you may need to use std::stoull() to convert them to usable numbers).A Critical Warning: Do Not Block the Main ThreadBecause ofxHTTP::Client.execute() is a synchronous, blocking call, placing it directly in your update() loop will freeze your graphics while it waits for the server to reply.To prevent stuttering, you must offload the request:Use ofThread: Wrap your API calls in a custom class extending ofThread. Lock a mutex when saving the parsed JSON, and unlock it when reading it in update().Use asynchronous features: If your version of ofxHTTP implements the DefaultClient or async event listeners, rely on those so the network response arrives via an event callback rather than hanging the main thread.

Information for this implementation:
TEZOS_CONTRACT=KT1DypSEV87pwiw6swdYqhDKWRBZ7xfqeS3c
TEZOS_NETWORK=shadownet
TEZOS_RPC=https://rpc.shadownet.teztnets.com
TZKT_BASE=https://api.shadownet.tzkt.io/v1
TEZOS_EXPLORER=https://shadownet.tzkt.io
TEZOS_MAX_BYTES=30000
TEZOS_POLL_SECONDS=30
