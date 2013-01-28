#include <gmock/gmock.h>

#include "../data_array.h"

class MockDataArray : public DataArray {
 public:
  MOCK_CONST_METHOD0(GetData, const vector<int>&());
  MOCK_CONST_METHOD1(AtIndex, int(int index));
  MOCK_CONST_METHOD0(GetSize, int());
  MOCK_CONST_METHOD0(GetVocabularySize, int());
  MOCK_CONST_METHOD1(HasWord, bool(const string& word));
  MOCK_CONST_METHOD1(GetWordId, int(const string& word));
  MOCK_CONST_METHOD1(GetSentenceId, int(int position));
};
