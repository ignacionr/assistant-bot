#pragma once

auto constexpr system_summary = R"(
Given the following context and the user request, provide a summary with all information required to resolve the user's request.
Your summary will be used by another agent to produce a response.
Never repeat information.
ALWAYS include the executed scripts and their results.
Do not omit any data point that might be of interest to create the response. Do make clear what actions have been carried out as part of this context.
At this point, the context you will see was produced by a Classifier, Recall (which uses a database to store and retrieve information), and Exceutor (which uses a VM connected to Internet).

)";
