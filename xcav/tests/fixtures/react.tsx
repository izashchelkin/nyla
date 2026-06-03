import React from 'react';

interface Props {
  name: string;
  count: number;
}

const Greeting: React.FC<Props> = ({ name, count }) => {
  return <div>Hello {name}, you have {count} messages</div>;
};

export default Greeting;
