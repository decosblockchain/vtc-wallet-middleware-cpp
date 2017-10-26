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
#include "httpserver.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <vector>
#include <memory>
#include <cstdlib>
#include <restbed>
#include "json.hpp"

using namespace std;
using namespace restbed;
using json = nlohmann::json;

VtcBlockIndexer::HttpServer::HttpServer(leveldb::DB* dbInstance) {
    this->db = dbInstance;
    
    httpClient.reset(new jsonrpc::HttpClient("http://middleware:middleware@vertcoind:8332"));
    vertcoind.reset(new VertcoinClient(*httpClient));
}

void VtcBlockIndexer::HttpServer::getTransaction(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    cout << "Looking up txid " << request->get_path_parameter("id") << endl;
    
    try {
        const Json::Value tx = vertcoind->getrawtransaction(request->get_path_parameter("id"), true);
        
        stringstream body;
        body << tx.toStyledString();
        
        session->close(OK, body.str(), {{"Content-Length",  std::to_string(body.str().size())}});
    } catch(const jsonrpc::JsonRpcException& e) {
        const std::string message(e.what());
        cout << "Not found " << message << endl;
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
    }
}

void VtcBlockIndexer::HttpServer::addressBalance( const shared_ptr< Session > session )
{
    long long balance = 0;
    int txoCount = 0;
    const auto request = session->get_request( );
    
    cout << "Checking balance for address " << request->get_path_parameter( "address" ) << endl;

    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        txoCount++;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        if(!s.ok()) // no key found, not spent. Add balance.
        {
            balance += stoll(txo.substr(80));
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    cout << "Analyzed " << txoCount << " TXOs - Balance is " << balance << endl;
 
    stringstream body;
    body << balance;
    
    stringstream bodyLength;
    bodyLength << body.str().size();

    session->close( OK, body.str(), { { "Content-Length",  bodyLength.str() } } );
}

void VtcBlockIndexer::HttpServer::addressTxos( const shared_ptr< Session > session )
{
    json j = json::array();

    const auto request = session->get_request( );

    long long sinceBlock = stoll(request->get_path_parameter( "sinceBlock", "0" ));
    
    cout << "Checking balance for address " << request->get_path_parameter( "address" ) << endl;

    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        long long block = stoll(txo.substr(72,8));
        if(block >= sinceBlock) {
            json txoObj;
            txoObj["txhash"] = txo.substr(0,64);
            txoObj["vout"] = stoll(txo.substr(64,8));
            txoObj["block"] = block;
            txoObj["value"] = stoll(txo.substr(80));
            if(!s.ok()) {
                txoObj["spender"] = nullptr;
            } else {
                txoObj["spender"] = spentTx.substr(64, 128);
            }
            j.push_back(txoObj);
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;


    string body = j.dump();

    stringstream bodyLength;
    bodyLength << body.size();
     
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  bodyLength.str() } } );
}



void VtcBlockIndexer::HttpServer::run()
{
    auto addressBalanceResource = make_shared< Resource >( );
    addressBalanceResource->set_path( "/addressBalance/{address: .*}" );
    addressBalanceResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressBalance, this, std::placeholders::_1) );

    auto addressTxosResource = make_shared< Resource >( );
    addressTxosResource->set_path( "/addressTxos/{address: .*}" );
    addressTxosResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );

    auto addressTxosSinceBlockResource = make_shared< Resource >( );
    addressTxosSinceBlockResource->set_path( "/addressTxosSince/{sinceBlock: ^[0-9]*$}/{address: .*}" );
    addressTxosSinceBlockResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );
    
    auto getTransctionResource = make_shared<Resource>();
    getTransctionResource->set_path( "/getTransaction/{id: [0-9a-f]*}" );
    getTransctionResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getTransaction, this, std::placeholders::_1) );


    auto settings = make_shared< Settings >( );
    settings->set_port( 8888 );
    settings->set_default_header( "Connection", "close" );

    Service service;
    service.publish( addressBalanceResource );
    service.publish( addressTxosResource );
    service.publish( addressTxosSinceBlockResource );
    service.publish( getTransctionResource );
    service.start( settings );
}
