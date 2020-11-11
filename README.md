<!--- Licensed to the Apache Software Foundation (ASF) under one -->
<!--- or more contributor license agreements.  See the NOTICE file -->
<!--- distributed with this work for additional information -->
<!--- regarding copyright ownership.  The ASF licenses this file -->
<!--- to you under the Apache License, Version 2.0 (the -->
<!--- "License"); you may not use this file except in compliance -->
<!--- with the License.  You may obtain a copy of the License at -->

<!---   http://www.apache.org/licenses/LICENSE-2.0 -->

<!--- Unless required by applicable law or agreed to in writing, -->
<!--- software distributed under the License is distributed on an -->
<!--- "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY -->
<!--- KIND, either express or implied.  See the License for the -->
<!--- specific language governing permissions and limitations -->
<!--- under the License. -->

Instructions for recreating the Error 
=====================================
- First back-up your existing files
- replace the measure_methods.py file located in `tvm_root/python/tvm/autotvm/measure` with one found in this repository
- compile the TVM by following cmd arguments `cd python; python setup.py install --user; cd ..`
- Run any client example.

For server side:
- A C++ RPC server should be located in `tvm_root/apps/cpp_rpc` It can be compiled to generate a executable server.
- Run a C++ RPC server with following commands replacing the IP address and port with right IP address and port `./tvm_rpc server --host=192.168.0.90 --port=9000 --port-end=9090 --tracker=192.168.0.20:9190 --key=v100`

Running the experiment
Run any client example from client node.
Logs should soon show the errors in both client and server side

<img src=https://raw.githubusercontent.com/apache/incubator-tvm-site/master/images/logo/tvm-logo-small.png width=128/> Open Deep Learning Compiler Stack
==============================================
[Documentation](https://tvm.apache.org/docs) |
[Contributors](CONTRIBUTORS.md) |
[Community](https://tvm.apache.org/community) |
[Release Notes](NEWS.md)

[![Build Status](https://ci.tvm.ai/buildStatus/icon?job=tvm/master)](https://ci.tvm.ai/job/tvm/job/master/)
[![WinMacBuild](https://github.com/apache/incubator-tvm/workflows/WinMacBuild/badge.svg)](https://github.com/apache/incubator-tvm/actions?query=workflow%3AWinMacBuild)

Apache TVM (incubating) is a compiler stack for deep learning systems. It is designed to close the gap between the
productivity-focused deep learning frameworks, and the performance- and efficiency-focused hardware backends.
TVM works with deep learning frameworks to provide end to end compilation to different backends.

License
-------
© Contributors Licensed under an [Apache-2.0](LICENSE) license.

Contribute to TVM
-----------------
TVM adopts apache committer model, we aim to create an open source project that is maintained and owned by the community.
Checkout the [Contributor Guide](https://tvm.apache.org/docs/contribute/)

Acknowledgement
---------------
We learned a lot from the following projects when building TVM.
- [Halide](https://github.com/halide/Halide): Part of TVM's TIR and arithmetic simplification module
  originates from Halide. We also learned and adapted some part of lowering pipeline from Halide.
- [Loopy](https://github.com/inducer/loopy): use of integer set analysis and its loop transformation primitives.
- [Theano](https://github.com/Theano/Theano): the design inspiration of symbolic scan operator for recurrence.
